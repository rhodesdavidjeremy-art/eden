#pragma once

#include <vector>

#include "common/Result.hpp"
#include "db/Database.hpp"
#include "model/Models.hpp"

namespace eden {

class CommentRepository {
public:
    explicit CommentRepository(Database& db) : db_(db) {}

    Result<Comment> insert(Comment comment);
    Result<Comment> get(const Id& id);
    Result<std::vector<Comment>> listByCard(const Id& cardId);
    Result<std::vector<Comment>> listReplies(const Id& parentId);
    Result<std::vector<Comment>> listByUser(const Id& userId, int limit);
    Result<std::int64_t> countByCard(const Id& cardId);
    Status editBody(const Id& id, const Id& editorId, const std::string& body);
    Status setPrivacy(const Id& id, bool isPrivate);
    Status setHidden(const Id& id, bool hidden);
    Status remove(const Id& id, const Id& requesterId);

    Status addReaction(const Id& commentId, const Id& userId, const std::string& reaction);
    Status removeReaction(const Id& commentId, const Id& userId, const std::string& reaction);
    Result<std::vector<ReactionCount>> reactionCounts(const Id& commentId);

private:
    Database& db_;
};

}  // namespace eden
