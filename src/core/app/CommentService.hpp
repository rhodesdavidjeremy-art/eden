#pragma once

#include <string>
#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

enum class MediaClass { Unknown, Image, Video, Audio };

struct MediaInfo {
    MediaClass kind = MediaClass::Unknown;
    std::string mime;
    std::string extension;
};

MediaInfo sniffMedia(const Bytes& data);
bool mediaMatchesCommentKind(const MediaInfo& info, CommentKind kind);

struct CommentView {
    Comment comment;
    std::string authorName;
    int depth = 0;
    std::vector<ReactionCount> reactions;
    std::vector<std::string> viewerReactions;
    bool canEdit = false;
    bool canDelete = false;
    bool canModerate = false;
    std::string mediaPath;
};

class CommentService {
public:
    CommentService(Database& db, std::string mediaDirectory, Id viewerId)
        : db_(db), mediaDirectory_(std::move(mediaDirectory)), viewerId_(std::move(viewerId)) {}

    static constexpr int kMaxDepth = 6;
    static const std::vector<std::string>& jewelReactions();

    Result<Comment> addText(const Id& cardId, const std::string& body, const Id& parentId = "",
                            std::int64_t anchorOffset = -1, bool isPrivate = true);
    Result<Comment> addMedia(const Id& cardId, CommentKind kind, const std::string& filePath,
                             const std::string& caption = "", const Id& parentId = "", std::int64_t anchorOffset = -1,
                             std::int64_t mediaTimestampMs = 0, bool isPrivate = true);
    Result<Comment> addWriterOriginal(const Id& cardId, const std::string& body, const std::string& mediaFile = "");

    Status edit(const Id& commentId, const std::string& body);
    Status remove(const Id& commentId);
    Status react(const Id& commentId, const std::string& reaction);
    Status unreact(const Id& commentId, const std::string& reaction);
    Status setPrivate(const Id& commentId, bool isPrivate);
    Status moderate(const Id& commentId, bool hide);

    Result<std::vector<CommentView>> thread(const Id& cardId);
    std::string mediaPath(const std::string& uri) const;
    Result<Id> documentOwnerForCard(const Id& cardId);

private:
    Result<std::string> storeMedia(const std::string& filePath, CommentKind kind);
    Status validateParent(const Id& cardId, const Id& parentId);

    Database& db_;
    std::string mediaDirectory_;
    Id viewerId_;
};

}  // namespace eden
