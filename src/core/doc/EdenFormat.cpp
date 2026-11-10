#include "doc/EdenFormat.hpp"

#include <filesystem>
#include <map>
#include <set>

#include "app/CommentService.hpp"
#include "common/Clock.hpp"
#include "common/Hash.hpp"
#include "common/Strings.hpp"
#include "doc/TextUtil.hpp"
#include "doc/Zip.hpp"
#include "repo/CardRepository.hpp"
#include "repo/CommentRepository.hpp"
#include "repo/DocumentRepository.hpp"
#include "repo/DocumentTextRepository.hpp"
#include "repo/PackRepository.hpp"
#include "repo/SettingsRepository.hpp"
#include "repo/SourceRepository.hpp"
#include "repo/UserRepository.hpp"

namespace eden {

namespace {

constexpr std::size_t kMaxEntries = 20000;
constexpr std::size_t kMaxTextBytes = 256u * 1024u * 1024u;

std::string baseName(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

Json sourceToJson(const Source& source) {
    return Json::object({{"url", Json(source.url)}, {"title", Json(source.title)}, {"publisher", Json(source.publisher)},
                         {"snippet", Json(source.snippet)}, {"trust", Json(toString(source.trust))}});
}

}  // namespace

bool isSafeArchiveName(const std::string& name) {
    if (name.empty() || name[0] == '/' || name.find('\\') != std::string::npos || name.find('\0') != std::string::npos) return false;
    for (const std::string& part : split(name, '/', false)) {
        if (part == ".." || part == ".") return false;
    }
    return true;
}

bool looksLikeEden(const Bytes& data) {
    if (data.size() < 30 || data[0] != 'P' || data[1] != 'K') return false;
    auto zip = ZipReader::open(data);
    return zip.ok() && zip.value().has("eden.json");
}

Result<Bytes> buildEnhancedDocument(Database& db, const Id& docId, const Id& publisherId, const std::string& mediaDirectory,
                                    const EdenPublishOptions& options) {
    DocumentRepository documents(db);
    auto document = documents.get(docId);
    if (!document.ok()) return document.error();
    if (document.value().ownerId != publisherId) return makeError(ErrorCode::Conflict, "only the document's owner can publish it");
    DocumentTextRepository texts(db);
    auto content = texts.get(docId);
    if (!content.ok()) return content.error();
    UserRepository users(db);
    auto owner = users.get(publisherId);
    const std::string author = !options.authorName.empty() ? options.authorName : (owner.ok() ? owner.value().displayName : "Author");

    PackRepository packRepository(db);
    auto allPacks = packRepository.listByDocument(docId);
    if (!allPacks.ok()) return allPacks.error();
    std::set<Id> selected(options.packIds.begin(), options.packIds.end());
    std::vector<ResearchPack> packs;
    for (const ResearchPack& pack : allPacks.value()) {
        if (selected.empty() || selected.count(pack.id)) packs.push_back(pack);
    }
    if (packs.empty()) return makeError(ErrorCode::InvalidArgument, "no research packs selected for publishing");

    ZipWriter zip;
    CardRepository cards(db);
    CommentRepository comments(db);
    SourceRepository sources(db);
    Json packList = Json::array();
    std::int64_t cardCount = 0;
    std::set<std::string> mediaWritten;
    auto addMedia = [&](const std::string& folder, const std::string& name) {
        if (name.empty() || !isSafeArchiveName(name)) return false;
        const std::string entry = folder + "/" + baseName(name);
        if (mediaWritten.count(entry)) return true;
        auto bytes = readFileBytes(mediaDirectory + "/" + baseName(name));
        if (!bytes.ok()) bytes = readFileBytes(name);
        if (!bytes.ok()) return false;
        zip.add(entry, bytes.value());
        mediaWritten.insert(entry);
        return true;
    };

    for (const ResearchPack& pack : packs) {
        packList.push(Json::object({{"id", Json(pack.id)}, {"start", Json(pack.startOffset)}, {"end", Json(pack.endOffset)},
                                    {"order", Json(pack.orderIndex)}, {"page", Json(pack.pageIndex)}}));
        auto card = cards.getByPack(pack.id);
        if (!card.ok()) continue;
        Json cardJson = Json::object();
        cardJson["pack_id"] = pack.id;
        cardJson["summary"] = card.value().summaryText;
        Json sections = Json::array();
        auto stored = cards.listSections(card.value().id);
        if (stored.ok()) {
            for (const CardSection& section : stored.value()) {
                sections.push(Json::object({{"function_id", Json(section.functionId)}, {"provider", Json(section.provider)},
                                            {"content", Json(section.content)}}));
            }
        }
        cardJson["sections"] = sections;
        Json sourceList = Json::array();
        auto listedSources = sources.listByCard(card.value().id);
        if (listedSources.ok()) {
            for (const Source& source : listedSources.value()) sourceList.push(sourceToJson(source));
        }
        cardJson["sources"] = sourceList;
        Json commentList = Json::array();
        auto listedComments = comments.listByCard(card.value().id);
        if (listedComments.ok()) {
            for (const Comment& comment : listedComments.value()) {
                if (!comment.isWriterOriginal || comment.hidden) continue;
                std::string mediaName = comment.mediaUri;
                if (!mediaName.empty() && !addMedia("media", mediaName)) mediaName.clear();
                commentList.push(Json::object({{"kind", Json(toString(comment.kind))}, {"body", Json(comment.body)},
                                               {"media", Json(baseName(mediaName))}, {"anchor_offset", Json(comment.anchorOffset)},
                                               {"media_timestamp_ms", Json(comment.mediaTimestampMs)}}));
            }
        }
        cardJson["comments"] = commentList;
        Json imageList = Json::array();
        if (options.includeImages) {
            auto images = Json::parse(card.value().aiImagesJson);
            if (images.ok()) {
                for (const Json& item : images.value().items()) {
                    const std::string path = item.isString() ? item.asString() : item.getString("path");
                    if (addMedia("images", path)) imageList.push(Json(baseName(path)));
                }
            }
        }
        cardJson["images"] = imageList;
        const bool hasContent = !card.value().summaryText.empty() || !sections.empty() || !commentList.empty();
        if (!hasContent) continue;
        zip.addText("cards/" + pack.id + ".json", cardJson.dump());
        ++cardCount;
    }

    Json prompts = Json::array();
    for (const ResearchPack& pack : packs) {
        prompts.push(Json::object({{"pack_id", Json(pack.id)}, {"functions", Json::arrayOfStrings(options.suggestedPrompts)}}));
    }
    Json manifest = Json::object();
    manifest["format"] = "eden-enhanced";
    manifest["version"] = kEdenFormatVersion;
    manifest["title"] = document.value().title;
    manifest["author"] = author;
    manifest["description"] = options.description;
    manifest["created"] = formatIso8601(nowMillis());
    manifest["language"] = content.value().language;
    Json source = Json::object({{"filename", Json(baseName(document.value().filePath))}, {"type", Json(toString(document.value().fileType))},
                                {"sha256", Json(document.value().contentHash)}});
    std::string originalEntry;
    if (options.includeOriginal && !document.value().filePath.empty()) {
        auto original = readFileBytes(document.value().filePath);
        if (original.ok()) {
            originalEntry = "source/" + baseName(document.value().filePath);
            zip.add(originalEntry, original.value());
            source["bytes"] = static_cast<std::int64_t>(original.value().size());
        }
    }
    manifest["source"] = source;
    manifest["stats"] = Json::object({{"packs", Json(static_cast<std::int64_t>(packs.size()))}, {"cards", Json(cardCount)},
                                      {"pages", Json(static_cast<std::int64_t>(content.value().pages.size()))},
                                      {"characters", Json(content.value().charCount())}});
    manifest["generator"] = "Eden Research Reader";
    zip.addText("eden.json", manifest.dump(2));
    zip.addText("text/document.txt", content.value().text);
    zip.addText("text/layout.json", Json::object({{"pages", pagesToJson(content.value().pages)},
                                                  {"chapters", chaptersToJson(content.value().chapters)},
                                                  {"title", Json(content.value().title)}}).dump());
    zip.addText("packs.json", packList.dump());
    zip.addText("prompts.json", prompts.dump());
    return zip.finish();
}

Result<ImportResult> importEnhancedDocument(Database& db, const Bytes& data, const ImportOptions& options) {
    if (options.ownerId.empty()) return makeError(ErrorCode::InvalidArgument, "import needs an owner");
    auto opened = ZipReader::open(data);
    if (!opened.ok()) return opened.error();
    const ZipReader& zip = opened.value();
    if (zip.entries().size() > kMaxEntries) return makeError(ErrorCode::InvalidArgument, "archive has too many entries");
    for (const ZipEntry& entry : zip.entries()) {
        if (!isSafeArchiveName(entry.name)) return makeError(ErrorCode::InvalidArgument, "unsafe path in archive: " + entry.name);
        if (entry.uncompressedSize > kMaxTextBytes) return makeError(ErrorCode::InvalidArgument, "archive entry is too large: " + entry.name);
    }
    auto manifestText = zip.readText("eden.json");
    if (!manifestText.ok()) return manifestText.error();
    auto manifestParsed = Json::parse(manifestText.value());
    if (!manifestParsed.ok()) return manifestParsed.error();
    const Json& manifest = manifestParsed.value();
    if (manifest.getString("format") != "eden-enhanced") return makeError(ErrorCode::Unsupported, "not an enhanced Eden document");
    if (manifest.getInt("version") > kEdenFormatVersion) {
        return makeError(ErrorCode::Unsupported, "this document needs a newer version of Eden Research Reader");
    }
    auto text = zip.readText("text/document.txt");
    if (!text.ok()) return text.error();
    if (!isValidUtf8(text.value())) return makeError(ErrorCode::Parse, "document text is not valid UTF-8");
    const std::int64_t length = static_cast<std::int64_t>(text.value().size());
    if (length == 0) return makeError(ErrorCode::Parse, "document text is empty");
    auto layoutText = zip.readText("text/layout.json");
    auto layout = layoutText.ok() ? Json::parse(layoutText.value()) : Result<Json>(Json::object());
    auto packsText = zip.readText("packs.json");
    if (!packsText.ok()) return packsText.error();
    auto packsParsed = Json::parse(packsText.value());
    if (!packsParsed.ok()) return packsParsed.error();

    ExtractedDocument extracted;
    extracted.type = FileType::Eden;
    extracted.text = text.value();
    extracted.title = manifest.getString("title");
    extracted.author = manifest.getString("author");
    extracted.language = manifest.getString("language");
    if (layout.ok()) {
        extracted.pages = pagesFromJson(layout.value().get("pages"));
        extracted.chapters = chaptersFromJson(layout.value().get("chapters"));
    }
    std::int64_t expectedStart = 0;
    for (const PageSpan& page : extracted.pages) {
        if (page.start != expectedStart || page.end < page.start || page.end > length) {
            extracted.pages.clear();
            break;
        }
        expectedStart = page.end;
    }
    if (extracted.pages.empty()) paginateByLength(extracted.text, 0, length, 3000, 0, extracted.pages);
    if (extracted.title.empty()) extracted.title = "Untitled";

    std::string hash = manifest.get("source").getString("sha256");
    if (hash.empty()) hash = sha256Hex(extracted.text);
    DocumentRepository documents(db);
    auto existing = documents.findByHash(options.ownerId, hash);
    if (existing.ok()) {
        ImportResult result;
        result.document = existing.value();
        result.alreadyExisted = true;
        auto count = PackRepository(db).countByDocument(existing.value().id);
        result.packCount = count.ok() ? count.value() : 0;
        return result;
    }

    struct PendingPack {
        std::string oldId;
        ResearchPack pack;
    };
    std::vector<PendingPack> pending;
    for (const Json& item : packsParsed.value().items()) {
        const std::int64_t start = item.getInt("start", -1);
        const std::int64_t end = item.getInt("end", -1);
        if (start < 0 || end <= start || end > length) return makeError(ErrorCode::Parse, "pack boundaries fall outside the document text");
        ResearchPack pack;
        pack.startOffset = start;
        pack.endOffset = end;
        pack.text = extracted.text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
        if (trim(pack.text).empty()) return makeError(ErrorCode::Parse, "a research pack is empty");
        pack.orderIndex = item.getInt("order", static_cast<std::int64_t>(pending.size()));
        pack.pageIndex = item.getInt("page", extracted.pageIndexAt(start));
        pack.origin = PackOrigin::Author;
        pending.push_back({item.getString("id"), std::move(pack)});
    }
    if (pending.empty()) return makeError(ErrorCode::Parse, "document contains no research packs");

    ImportResult result;
    try {
        auto guard = db.lock();
        Transaction transaction(db);
        UserRepository users(db);
        const std::string authorName = manifest.getString("author", "Author");
        const std::string marker = "author:" + slugify(authorName);
        Id authorId;
        auto knownAuthor = users.getByEmail(marker);
        if (knownAuthor.ok()) {
            authorId = knownAuthor.value().id;
        } else {
            User author;
            author.displayName = authorName.empty() ? "Author" : authorName;
            author.email = marker;
            auto created = users.insert(author);
            if (!created.ok()) throw std::runtime_error(created.error().message);
            authorId = created.value().id;
        }

        Document document;
        document.title = extracted.title;
        document.fileType = FileType::Eden;
        document.ownerId = options.ownerId;
        document.isEnhanced = true;
        document.filePath = "";
        document.contentHash = hash;
        document.pageCount = static_cast<std::int64_t>(extracted.pages.size());
        document.charCount = length;
        auto inserted = documents.insert(document);
        if (!inserted.ok()) throw std::runtime_error(inserted.error().message);
        document = inserted.value();
        DocumentTextRepository textRepository(db);
        Status stored = textRepository.put(document.id, extracted);
        if (!stored.ok()) throw std::runtime_error(stored.error().message);

        std::vector<ResearchPack> packBatch;
        for (PendingPack& item : pending) {
            item.pack.docId = document.id;
            packBatch.push_back(item.pack);
        }
        PackRepository packs(db);
        auto created = packs.insertMany(std::move(packBatch));
        if (!created.ok()) throw std::runtime_error(created.error().message);
        std::map<std::string, Id> newIds;
        for (std::size_t i = 0; i < pending.size(); ++i) newIds[pending[i].oldId] = created.value()[i].id;
        result.packCount = static_cast<std::int64_t>(created.value().size());

        CardRepository cards(db);
        CommentRepository comments(db);
        SourceRepository sources(db);
        std::error_code fsError;
        for (const auto& [oldId, newId] : newIds) {
            if (oldId.empty()) continue;
            const std::string entry = "cards/" + oldId + ".json";
            if (!zip.has(entry)) continue;
            auto cardText = zip.readText(entry);
            if (!cardText.ok()) throw std::runtime_error(cardText.error().message);
            auto cardParsed = Json::parse(cardText.value());
            if (!cardParsed.ok()) throw std::runtime_error(cardParsed.error().message);
            const Json& cardJson = cardParsed.value();
            auto card = cards.getOrCreateByPack(newId);
            if (!card.ok()) throw std::runtime_error(card.error().message);
            ResearchCard record = card.value();
            record.summaryText = cardJson.getString("summary");
            Json images = Json::array();
            if (!options.imageDirectory.empty()) {
                std::filesystem::create_directories(options.imageDirectory, fsError);
                for (const Json& name : cardJson.get("images").items()) {
                    const std::string file = baseName(name.asString());
                    auto bytes = zip.read("images/" + file);
                    if (!bytes.ok() || !isSafeArchiveName(file)) continue;
                    const std::string target = options.imageDirectory + "/" + document.id.substr(0, 8) + "-" + file;
                    writeFileBytes(target, bytes.value());
                    images.push(Json::object({{"path", Json(target)}, {"caption", Json("")}}));
                }
            }
            record.aiImagesJson = images.dump();
            auto saved = cards.save(record);
            if (!saved.ok()) throw std::runtime_error(saved.error().message);
            for (const Json& section : cardJson.get("sections").items()) {
                CardSection item;
                item.cardId = card.value().id;
                item.functionId = section.getString("function_id");
                item.content = section.getString("content");
                item.provider = section.getString("provider");
                Status put = cards.putSection(item);
                if (!put.ok()) throw std::runtime_error(put.error().message);
            }
            for (const Json& entryJson : cardJson.get("sources").items()) {
                Source source;
                source.cardId = card.value().id;
                source.url = entryJson.getString("url");
                source.title = entryJson.getString("title");
                source.publisher = entryJson.getString("publisher");
                source.snippet = entryJson.getString("snippet");
                source.trust = sourceTrustFromString(entryJson.getString("trust"));
                if (source.url.empty()) continue;
                auto added = sources.add(source);
                if (!added.ok()) throw std::runtime_error(added.error().message);
            }
            for (const Json& item : cardJson.get("comments").items()) {
                Comment comment;
                comment.cardId = card.value().id;
                comment.userId = authorId;
                comment.isWriterOriginal = true;
                comment.isPrivate = false;
                comment.kind = commentKindFromString(item.getString("kind"));
                comment.body = item.getString("body");
                comment.anchorOffset = item.getInt("anchor_offset", -1);
                comment.mediaTimestampMs = item.getInt("media_timestamp_ms");
                const std::string media = baseName(item.getString("media"));
                if (!media.empty() && isSafeArchiveName(media)) {
                    auto bytes = zip.read("media/" + media);
                    if (bytes.ok() && !options.mediaDirectory.empty()) {
                        std::filesystem::create_directories(options.mediaDirectory, fsError);
                        const MediaInfo info = sniffMedia(bytes.value());
                        if (info.kind != MediaClass::Unknown && mediaMatchesCommentKind(info, comment.kind)) {
                            writeFileBytes(options.mediaDirectory + "/" + media, bytes.value());
                            comment.mediaUri = media;
                        }
                    }
                }
                if (comment.kind != CommentKind::Text && comment.mediaUri.empty()) {
                    result.warnings.push_back("a media comment was skipped because its attachment was missing or invalid");
                    continue;
                }
                if (comment.body.empty() && comment.mediaUri.empty()) continue;
                auto added = comments.insert(comment);
                if (!added.ok()) throw std::runtime_error(added.error().message);
            }
        }
        if (zip.has("prompts.json")) {
            auto promptText = zip.readText("prompts.json");
            auto promptJson = promptText.ok() ? Json::parse(promptText.value()) : Result<Json>(Json::array());
            if (promptJson.ok()) {
                Json remapped = Json::array();
                for (const Json& item : promptJson.value().items()) {
                    auto found = newIds.find(item.getString("pack_id"));
                    if (found == newIds.end()) continue;
                    remapped.push(Json::object({{"pack_id", Json(found->second)}, {"functions", item.get("functions")}}));
                }
                SettingsRepository(db).setJson("eden.prompts." + document.id, remapped);
            }
        }
        transaction.commit();
        result.document = document;
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::Internal, error.what());
    }
    return result;
}

}  // namespace eden
