#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common/Types.hpp"

namespace eden {

enum class FileType { Unknown, Pdf, Docx, Txt, Epub, Eden };
enum class PackOrigin { Manual, Auto, Author };
enum class CommentKind { Text, Image, Video, Voice, Camera };
enum class SourceTrust { Unrated, Low, Medium, High };
enum class RuleKind { Allow, Block };
enum class SessionStatus { Ok, Error, Cancelled };

const char* toString(FileType value);
const char* toString(PackOrigin value);
const char* toString(CommentKind value);
const char* toString(SourceTrust value);
const char* toString(RuleKind value);
const char* toString(SessionStatus value);

FileType fileTypeFromString(const std::string& text);
PackOrigin packOriginFromString(const std::string& text);
CommentKind commentKindFromString(const std::string& text);
SourceTrust sourceTrustFromString(const std::string& text);
RuleKind ruleKindFromString(const std::string& text);
SessionStatus sessionStatusFromString(const std::string& text);
FileType fileTypeFromPath(const std::string& path);

struct User {
    Id id;
    std::string email;
    std::string displayName;
    std::string themePrefsJson = "{}";
    Millis createdAt = 0;
};

struct Document {
    Id id;
    std::string title;
    FileType fileType = FileType::Unknown;
    Id ownerId;
    bool isEnhanced = false;
    std::string filePath;
    std::string contentHash;
    std::int64_t pageCount = 0;
    std::int64_t charCount = 0;
    Millis lastOpenedAt = 0;
    std::int64_t lastPosition = 0;
    Millis createdAt = 0;
};

struct ResearchPack {
    Id id;
    Id docId;
    std::int64_t startOffset = 0;
    std::int64_t endOffset = 0;
    std::string text;
    std::int64_t orderIndex = 0;
    std::int64_t pageIndex = 0;
    PackOrigin origin = PackOrigin::Manual;
    Millis createdAt = 0;
};

struct ResearchCard {
    Id id;
    Id packId;
    std::string summaryText;
    std::string evidenceJson = "[]";
    std::string aiImagesJson = "[]";
    std::string sourcesJson = "[]";
    std::int64_t revision = 1;
    Millis createdAt = 0;
    Millis updatedAt = 0;
};

struct CardSection {
    Id cardId;
    std::string functionId;
    std::string content;
    std::string provider;
    Millis createdAt = 0;
};

struct CardRevision {
    std::int64_t revisionId = 0;
    Id cardId;
    std::int64_t revision = 0;
    std::string snapshotJson;
    Millis createdAt = 0;
};

struct Comment {
    Id id;
    Id cardId;
    Id userId;
    CommentKind kind = CommentKind::Text;
    std::string mediaUri;
    std::string body;
    bool isWriterOriginal = false;
    bool isPrivate = true;
    bool hidden = false;
    std::int64_t anchorOffset = -1;
    std::int64_t mediaTimestampMs = 0;
    Id parentId;
    Millis createdAt = 0;
    Millis editedAt = 0;
};

struct ReactionCount {
    std::string reaction;
    std::int64_t count = 0;
};

struct AiSession {
    Id id;
    Id userId;
    Id packId;
    std::string provider;
    std::string model;
    std::string functionId;
    std::string prompt;
    std::string response;
    std::int64_t inputTokens = 0;
    std::int64_t outputTokens = 0;
    std::int64_t latencyMs = 0;
    SessionStatus status = SessionStatus::Ok;
    Millis createdAt = 0;
};

struct Collection {
    Id id;
    Id ownerId;
    std::string name;
    std::string description;
    bool isPublic = false;
    Millis createdAt = 0;
    Millis updatedAt = 0;
};

struct CollectionItem {
    Id collectionId;
    Id packId;
    std::int64_t position = 0;
    Millis addedAt = 0;
};

struct Source {
    Id id;
    Id cardId;
    std::string url;
    std::string title;
    std::string publisher;
    std::string snippet;
    SourceTrust trust = SourceTrust::Unrated;
    bool approved = false;
    Millis createdAt = 0;
};

struct SourceRule {
    std::int64_t id = 0;
    std::string domain;
    RuleKind kind = RuleKind::Allow;
    Millis createdAt = 0;
};

struct Bookmark {
    Id id;
    Id docId;
    std::int64_t charOffset = 0;
    std::string label;
    Millis createdAt = 0;
};

struct PackHit {
    ResearchPack pack;
    std::string snippet;
    double rank = 0.0;
};

}  // namespace eden
