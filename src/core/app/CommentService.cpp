#include "app/CommentService.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

#include "common/Hash.hpp"
#include "common/Strings.hpp"
#include "doc/TextUtil.hpp"
#include "doc/Zip.hpp"
#include "repo/CommentRepository.hpp"
#include "repo/UserRepository.hpp"

namespace eden {

namespace {

constexpr std::size_t kMaxImageBytes = 25u * 1024u * 1024u;
constexpr std::size_t kMaxAudioBytes = 50u * 1024u * 1024u;
constexpr std::size_t kMaxVideoBytes = 200u * 1024u * 1024u;

bool has(const Bytes& data, std::size_t offset, const char* text) {
    const std::size_t length = std::char_traits<char>::length(text);
    if (data.size() < offset + length) return false;
    for (std::size_t i = 0; i < length; ++i) {
        if (data[offset + i] != static_cast<std::uint8_t>(text[i])) return false;
    }
    return true;
}

std::size_t limitFor(MediaClass kind) {
    switch (kind) {
        case MediaClass::Image: return kMaxImageBytes;
        case MediaClass::Audio: return kMaxAudioBytes;
        case MediaClass::Video: return kMaxVideoBytes;
        case MediaClass::Unknown: break;
    }
    return 0;
}

}  // namespace

MediaInfo sniffMedia(const Bytes& data) {
    MediaInfo info;
    auto set = [&](MediaClass kind, const char* mime, const char* extension) {
        info.kind = kind;
        info.mime = mime;
        info.extension = extension;
        return info;
    };
    if (data.size() < 12) return info;
    if (has(data, 0, "\x89PNG")) return set(MediaClass::Image, "image/png", "png");
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return set(MediaClass::Image, "image/jpeg", "jpg");
    if (has(data, 0, "GIF8")) return set(MediaClass::Image, "image/gif", "gif");
    if (has(data, 0, "RIFF") && has(data, 8, "WEBP")) return set(MediaClass::Image, "image/webp", "webp");
    if (has(data, 0, "RIFF") && has(data, 8, "WAVE")) return set(MediaClass::Audio, "audio/wav", "wav");
    if (has(data, 0, "RIFF") && has(data, 8, "AVI ")) return set(MediaClass::Video, "video/x-msvideo", "avi");
    if (has(data, 0, "ID3") || (data[0] == 0xFF && (data[1] & 0xE0) == 0xE0)) return set(MediaClass::Audio, "audio/mpeg", "mp3");
    if (has(data, 0, "OggS")) return set(MediaClass::Audio, "audio/ogg", "ogg");
    if (has(data, 0, "fLaC")) return set(MediaClass::Audio, "audio/flac", "flac");
    if (has(data, 4, "ftyp")) {
        if (has(data, 8, "M4A ") || has(data, 8, "M4B ")) return set(MediaClass::Audio, "audio/mp4", "m4a");
        if (has(data, 8, "qt  ")) return set(MediaClass::Video, "video/quicktime", "mov");
        return set(MediaClass::Video, "video/mp4", "mp4");
    }
    if (data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3) return set(MediaClass::Video, "video/webm", "webm");
    return info;
}

bool mediaMatchesCommentKind(const MediaInfo& info, CommentKind kind) {
    switch (kind) {
        case CommentKind::Image: return info.kind == MediaClass::Image;
        case CommentKind::Video:
        case CommentKind::Camera: return info.kind == MediaClass::Video;
        case CommentKind::Voice: return info.kind == MediaClass::Audio || info.mime == "video/webm";
        case CommentKind::Text: return false;
    }
    return false;
}

const std::vector<std::string>& CommentService::jewelReactions() {
    static const std::vector<std::string> reactions = {"ruby", "emerald", "sapphire", "amethyst", "pearl", "gold"};
    return reactions;
}

std::string CommentService::mediaPath(const std::string& uri) const {
    if (uri.empty()) return std::string();
    return mediaDirectory_ + "/" + fileName(uri);
}

Result<Id> CommentService::documentOwnerForCard(const Id& cardId) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT d.owner_id FROM research_cards c JOIN research_packs p ON p.pack_id = c.pack_id "
            "JOIN documents d ON d.doc_id = p.doc_id WHERE c.card_id = :card");
        stmt.bindText(":card", cardId);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "card not found: " + cardId);
        return stmt.getText(0);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Status CommentService::validateParent(const Id& cardId, const Id& parentId) {
    if (parentId.empty()) return Status::success();
    CommentRepository comments(db_);
    int depth = 0;
    Id current = parentId;
    while (!current.empty()) {
        auto parent = comments.get(current);
        if (!parent.ok()) return makeError(ErrorCode::NotFound, "parent comment not found");
        if (parent.value().cardId != cardId) return makeError(ErrorCode::InvalidArgument, "reply belongs to a different card");
        if (++depth >= kMaxDepth) return makeError(ErrorCode::InvalidArgument, "reply thread is too deep");
        current = parent.value().parentId;
    }
    return Status::success();
}

Result<std::string> CommentService::storeMedia(const std::string& filePath, CommentKind kind) {
    auto bytes = readFileBytes(filePath);
    if (!bytes.ok()) return bytes.error();
    const MediaInfo info = sniffMedia(bytes.value());
    if (info.kind == MediaClass::Unknown) return makeError(ErrorCode::Unsupported, "unrecognized media format");
    if (!mediaMatchesCommentKind(info, kind)) {
        return makeError(ErrorCode::InvalidArgument, std::string("file is ") + info.mime + ", which does not fit a " + toString(kind) + " comment");
    }
    if (bytes.value().size() > limitFor(info.kind)) {
        return makeError(ErrorCode::InvalidArgument, "media file is too large (" + formatBytes(bytes.value().size()) + ")");
    }
    std::error_code error;
    std::filesystem::create_directories(mediaDirectory_, error);
    if (error) return makeError(ErrorCode::Io, "cannot create media directory: " + error.message());
    const std::string name = sha256Hex(std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size())) +
                             "." + info.extension;
    const std::string target = mediaDirectory_ + "/" + name;
    if (!std::filesystem::exists(target)) {
        Status written = writeFileBytes(target, bytes.value());
        if (!written.ok()) return written.error();
    }
    return name;
}

Result<Comment> CommentService::addText(const Id& cardId, const std::string& body, const Id& parentId,
                                        std::int64_t anchorOffset, bool isPrivate) {
    const std::string cleaned = trim(body);
    if (cleaned.empty()) return makeError(ErrorCode::InvalidArgument, "comment is empty");
    Status parent = validateParent(cardId, parentId);
    if (!parent.ok()) return parent.error();
    CommentRepository comments(db_);
    Comment comment;
    comment.cardId = cardId;
    comment.userId = viewerId_;
    comment.body = cleaned;
    comment.parentId = parentId;
    comment.anchorOffset = anchorOffset;
    comment.isPrivate = isPrivate;
    return comments.insert(comment);
}

Result<Comment> CommentService::addMedia(const Id& cardId, CommentKind kind, const std::string& filePath,
                                         const std::string& caption, const Id& parentId, std::int64_t anchorOffset,
                                         std::int64_t mediaTimestampMs, bool isPrivate) {
    if (kind == CommentKind::Text) return makeError(ErrorCode::InvalidArgument, "use addText for text comments");
    Status parent = validateParent(cardId, parentId);
    if (!parent.ok()) return parent.error();
    auto stored = storeMedia(filePath, kind);
    if (!stored.ok()) return stored.error();
    CommentRepository comments(db_);
    Comment comment;
    comment.cardId = cardId;
    comment.userId = viewerId_;
    comment.kind = kind;
    comment.mediaUri = stored.value();
    comment.body = trim(caption);
    comment.parentId = parentId;
    comment.anchorOffset = anchorOffset;
    comment.mediaTimestampMs = std::max<std::int64_t>(0, mediaTimestampMs);
    comment.isPrivate = isPrivate;
    return comments.insert(comment);
}

Result<Comment> CommentService::addWriterOriginal(const Id& cardId, const std::string& body, const std::string& mediaFile) {
    auto owner = documentOwnerForCard(cardId);
    if (!owner.ok()) return owner.error();
    if (owner.value() != viewerId_) {
        return makeError(ErrorCode::Conflict, "only the document's author can add writer-original comments");
    }
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT d.is_enhanced FROM research_cards c JOIN research_packs p ON p.pack_id = c.pack_id "
            "JOIN documents d ON d.doc_id = p.doc_id WHERE c.card_id = :card");
        stmt.bindText(":card", cardId);
        if (stmt.step() && stmt.getInt(0) != 0) {
            return makeError(ErrorCode::Immutable, "this is an imported Enhanced Eden document; writer-original comments belong to its author");
        }
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
    CommentRepository comments(db_);
    Comment comment;
    comment.cardId = cardId;
    comment.userId = viewerId_;
    comment.isWriterOriginal = true;
    comment.isPrivate = false;
    comment.body = trim(body);
    if (!mediaFile.empty()) {
        auto bytes = readFileBytes(mediaFile);
        if (!bytes.ok()) return bytes.error();
        const MediaInfo info = sniffMedia(bytes.value());
        comment.kind = info.kind == MediaClass::Image   ? CommentKind::Image
                       : info.kind == MediaClass::Video ? CommentKind::Video
                       : info.kind == MediaClass::Audio ? CommentKind::Voice
                                                        : CommentKind::Text;
        if (comment.kind == CommentKind::Text) return makeError(ErrorCode::Unsupported, "unrecognized media format");
        auto stored = storeMedia(mediaFile, comment.kind);
        if (!stored.ok()) return stored.error();
        comment.mediaUri = stored.value();
    }
    return comments.insert(comment);
}

Status CommentService::edit(const Id& commentId, const std::string& body) {
    CommentRepository comments(db_);
    return comments.editBody(commentId, viewerId_, trim(body));
}

Status CommentService::remove(const Id& commentId) {
    CommentRepository comments(db_);
    return comments.remove(commentId, viewerId_);
}

Status CommentService::react(const Id& commentId, const std::string& reaction) {
    const auto& allowed = jewelReactions();
    if (std::find(allowed.begin(), allowed.end(), reaction) == allowed.end()) {
        return makeError(ErrorCode::InvalidArgument, "unknown reaction: " + reaction);
    }
    CommentRepository comments(db_);
    auto existing = comments.get(commentId);
    if (!existing.ok()) return existing.error();
    return comments.addReaction(commentId, viewerId_, reaction);
}

Status CommentService::unreact(const Id& commentId, const std::string& reaction) {
    CommentRepository comments(db_);
    return comments.removeReaction(commentId, viewerId_, reaction);
}

Status CommentService::setPrivate(const Id& commentId, bool isPrivate) {
    CommentRepository comments(db_);
    auto existing = comments.get(commentId);
    if (!existing.ok()) return existing.error();
    if (existing.value().userId != viewerId_) return makeError(ErrorCode::Conflict, "only the author can change privacy");
    if (existing.value().isWriterOriginal) return makeError(ErrorCode::Immutable, "writer-original comments are always public");
    return comments.setPrivacy(commentId, isPrivate);
}

Status CommentService::moderate(const Id& commentId, bool hide) {
    CommentRepository comments(db_);
    auto existing = comments.get(commentId);
    if (!existing.ok()) return existing.error();
    auto owner = documentOwnerForCard(existing.value().cardId);
    if (!owner.ok()) return owner.error();
    if (owner.value() != viewerId_) return makeError(ErrorCode::Conflict, "only the document's author can moderate comments");
    if (existing.value().isWriterOriginal) return makeError(ErrorCode::Immutable, "writer-original comments cannot be hidden");
    return comments.setHidden(commentId, hide);
}

Result<std::vector<CommentView>> CommentService::thread(const Id& cardId) {
    CommentRepository comments(db_);
    auto listed = comments.listByCard(cardId);
    if (!listed.ok()) return listed.error();
    auto owner = documentOwnerForCard(cardId);
    const bool moderator = owner.ok() && owner.value() == viewerId_;

    std::map<Id, std::vector<const Comment*>> children;
    std::vector<const Comment*> roots;
    std::set<Id> visibleIds;
    for (const Comment& comment : listed.value()) {
        const bool own = comment.userId == viewerId_;
        if (comment.hidden && !moderator && !own) continue;
        if (comment.isPrivate && !comment.isWriterOriginal && !own) continue;
        visibleIds.insert(comment.id);
    }
    for (const Comment& comment : listed.value()) {
        if (!visibleIds.count(comment.id)) continue;
        if (comment.parentId.empty()) {
            roots.push_back(&comment);
        } else if (visibleIds.count(comment.parentId)) {
            children[comment.parentId].push_back(&comment);
        }
    }
    UserRepository users(db_);
    std::map<Id, std::string> names;
    auto nameOf = [&](const Id& userId) {
        auto it = names.find(userId);
        if (it != names.end()) return it->second;
        auto user = users.get(userId);
        return names[userId] = user.ok() ? user.value().displayName : std::string("Unknown");
    };
    std::vector<CommentView> out;
    std::function<void(const Comment&, int)> emit = [&](const Comment& comment, int depth) {
        CommentView view;
        view.comment = comment;
        view.authorName = nameOf(comment.userId);
        view.depth = depth;
        auto counts = comments.reactionCounts(comment.id);
        if (counts.ok()) view.reactions = counts.value();
        try {
            auto guard = db_.lock();
            Statement stmt = db_.prepare("SELECT reaction FROM comment_reactions WHERE comment_id = :c AND user_id = :u");
            stmt.bindText(":c", comment.id);
            stmt.bindText(":u", viewerId_);
            while (stmt.step()) view.viewerReactions.push_back(stmt.getText(0));
        } catch (const DbError&) {
        }
        const bool own = comment.userId == viewerId_;
        view.canEdit = own && !comment.isWriterOriginal && comment.kind == CommentKind::Text;
        view.canDelete = own && !comment.isWriterOriginal;
        view.canModerate = moderator && !comment.isWriterOriginal;
        view.mediaPath = mediaPath(comment.mediaUri);
        out.push_back(std::move(view));
        auto found = children.find(comment.id);
        if (found == children.end()) return;
        for (const Comment* child : found->second) emit(*child, depth + 1);
    };
    for (const Comment* root : roots) emit(*root, 0);
    return out;
}

}  // namespace eden
