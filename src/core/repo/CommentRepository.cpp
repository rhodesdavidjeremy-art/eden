#include "repo/CommentRepository.hpp"

#include "common/Clock.hpp"
#include "common/Ids.hpp"

namespace eden {

namespace {

const char* kColumns =
    "comment_id, card_id, user_id, content_type, media_uri, body_text, is_writer_original, "
    "is_private, anchor_offset, media_timestamp_ms, COALESCE(parent_comment_id, ''), created_at, "
    "edited_at, hidden";

Comment readComment(Statement& stmt) {
    Comment comment;
    comment.id = stmt.getText(0);
    comment.cardId = stmt.getText(1);
    comment.userId = stmt.getText(2);
    comment.kind = commentKindFromString(stmt.getText(3));
    comment.mediaUri = stmt.getText(4);
    comment.body = stmt.getText(5);
    comment.isWriterOriginal = stmt.getInt(6) != 0;
    comment.isPrivate = stmt.getInt(7) != 0;
    comment.anchorOffset = stmt.getInt(8);
    comment.mediaTimestampMs = stmt.getInt(9);
    comment.parentId = stmt.getText(10);
    comment.createdAt = stmt.getInt(11);
    comment.editedAt = stmt.getInt(12);
    comment.hidden = stmt.getInt(13) != 0;
    return comment;
}

Status validate(const Comment& comment) {
    if (comment.cardId.empty()) return makeError(ErrorCode::InvalidArgument, "comment needs a card");
    if (comment.userId.empty()) return makeError(ErrorCode::InvalidArgument, "comment needs an author");
    if (comment.kind == CommentKind::Text && comment.body.empty()) {
        return makeError(ErrorCode::InvalidArgument, "text comment body is empty");
    }
    if (comment.kind != CommentKind::Text && comment.mediaUri.empty()) {
        return makeError(ErrorCode::InvalidArgument, "media comment needs a media uri");
    }
    if (comment.isWriterOriginal && !comment.parentId.empty()) {
        return makeError(ErrorCode::InvalidArgument, "writer-original comments cannot be replies");
    }
    return Status::success();
}

}  // namespace

Result<Comment> CommentRepository::insert(Comment comment) {
    Status check = validate(comment);
    if (!check.ok()) return check.error();
    return guarded<Comment>([&]() {
        auto guard = db_.lock();
        if (comment.id.empty()) comment.id = newId();
        if (comment.createdAt == 0) comment.createdAt = nowMillis();
        Statement stmt = db_.prepare(
            "INSERT INTO comments(comment_id, card_id, user_id, content_type, media_uri, body_text, "
            "is_writer_original, is_private, anchor_offset, media_timestamp_ms, parent_comment_id, "
            "created_at, edited_at) VALUES(:id, :card, :user, :type, :media, :body, :writer, :private, "
            ":anchor, :mediats, :parent, :created, 0)");
        stmt.bindText(":id", comment.id);
        stmt.bindText(":card", comment.cardId);
        stmt.bindText(":user", comment.userId);
        stmt.bindText(":type", toString(comment.kind));
        stmt.bindText(":media", comment.mediaUri);
        stmt.bindText(":body", comment.body);
        stmt.bindInt(":writer", comment.isWriterOriginal ? 1 : 0);
        stmt.bindInt(":private", comment.isPrivate ? 1 : 0);
        stmt.bindInt(":anchor", comment.anchorOffset);
        stmt.bindInt(":mediats", comment.mediaTimestampMs);
        if (comment.parentId.empty()) {
            stmt.bindNull(":parent");
        } else {
            stmt.bindText(":parent", comment.parentId);
        }
        stmt.bindInt(":created", comment.createdAt);
        stmt.execute();
        return comment;
    });
}

Result<Comment> CommentRepository::get(const Id& id) {
    try {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM comments WHERE comment_id = :id");
        stmt.bindText(":id", id);
        if (!stmt.step()) return makeError(ErrorCode::NotFound, "comment not found: " + id);
        return readComment(stmt);
    } catch (const DbError& error) {
        return makeError(ErrorCode::Database, error.what());
    }
}

Result<std::vector<Comment>> CommentRepository::listByCard(const Id& cardId) {
    return guarded<std::vector<Comment>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM comments WHERE card_id = :card "
                                     "ORDER BY is_writer_original DESC, created_at ASC");
        stmt.bindText(":card", cardId);
        std::vector<Comment> out;
        while (stmt.step()) out.push_back(readComment(stmt));
        return out;
    });
}

Result<std::vector<Comment>> CommentRepository::listReplies(const Id& parentId) {
    return guarded<std::vector<Comment>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM comments WHERE parent_comment_id = :parent ORDER BY created_at");
        stmt.bindText(":parent", parentId);
        std::vector<Comment> out;
        while (stmt.step()) out.push_back(readComment(stmt));
        return out;
    });
}

Result<std::vector<Comment>> CommentRepository::listByUser(const Id& userId, int limit) {
    return guarded<std::vector<Comment>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(std::string("SELECT ") + kColumns +
                                     " FROM comments WHERE user_id = :user ORDER BY created_at DESC LIMIT :limit");
        stmt.bindText(":user", userId);
        stmt.bindInt(":limit", limit);
        std::vector<Comment> out;
        while (stmt.step()) out.push_back(readComment(stmt));
        return out;
    });
}

Result<std::int64_t> CommentRepository::countByCard(const Id& cardId) {
    return guarded<std::int64_t>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("SELECT COUNT(*) FROM comments WHERE card_id = :card");
        stmt.bindText(":card", cardId);
        stmt.step();
        return stmt.getInt(0);
    });
}

Status CommentRepository::editBody(const Id& id, const Id& editorId, const std::string& body) {
    auto existing = get(id);
    if (!existing.ok()) return existing.error();
    if (existing.value().isWriterOriginal) {
        return makeError(ErrorCode::Immutable, "writer-original comments cannot be edited");
    }
    if (existing.value().userId != editorId) {
        return makeError(ErrorCode::Conflict, "only the author can edit a comment");
    }
    if (existing.value().kind == CommentKind::Text && body.empty()) {
        return makeError(ErrorCode::InvalidArgument, "text comment body is empty");
    }
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "UPDATE comments SET body_text = :body, edited_at = :now WHERE comment_id = :id");
        stmt.bindText(":body", body);
        stmt.bindInt(":now", nowMillis());
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status CommentRepository::setHidden(const Id& id, bool hidden) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("UPDATE comments SET hidden = :hidden WHERE comment_id = :id");
        stmt.bindInt(":hidden", hidden ? 1 : 0);
        stmt.bindText(":id", id);
        stmt.execute();
        if (db_.changes() == 0) throw std::runtime_error("comment not found: " + id);
    });
}

Status CommentRepository::setPrivacy(const Id& id, bool isPrivate) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "UPDATE comments SET is_private = :private WHERE comment_id = :id AND is_writer_original = 0");
        stmt.bindInt(":private", isPrivate ? 1 : 0);
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status CommentRepository::remove(const Id& id, const Id& requesterId) {
    auto existing = get(id);
    if (!existing.ok()) return existing.error();
    if (existing.value().isWriterOriginal) {
        return makeError(ErrorCode::Immutable, "writer-original comments cannot be deleted");
    }
    if (existing.value().userId != requesterId) {
        return makeError(ErrorCode::Conflict, "only the author can delete a comment");
    }
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare("DELETE FROM comments WHERE comment_id = :id");
        stmt.bindText(":id", id);
        stmt.execute();
    });
}

Status CommentRepository::addReaction(const Id& commentId, const Id& userId, const std::string& reaction) {
    if (reaction.empty()) return makeError(ErrorCode::InvalidArgument, "reaction is empty");
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "INSERT OR IGNORE INTO comment_reactions(comment_id, user_id, reaction, created_at) "
            "VALUES(:comment, :user, :reaction, :now)");
        stmt.bindText(":comment", commentId);
        stmt.bindText(":user", userId);
        stmt.bindText(":reaction", reaction);
        stmt.bindInt(":now", nowMillis());
        stmt.execute();
    });
}

Status CommentRepository::removeReaction(const Id& commentId, const Id& userId, const std::string& reaction) {
    return guardedStatus([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "DELETE FROM comment_reactions WHERE comment_id = :comment AND user_id = :user AND reaction = :reaction");
        stmt.bindText(":comment", commentId);
        stmt.bindText(":user", userId);
        stmt.bindText(":reaction", reaction);
        stmt.execute();
    });
}

Result<std::vector<ReactionCount>> CommentRepository::reactionCounts(const Id& commentId) {
    return guarded<std::vector<ReactionCount>>([&]() {
        auto guard = db_.lock();
        Statement stmt = db_.prepare(
            "SELECT reaction, COUNT(*) FROM comment_reactions WHERE comment_id = :comment "
            "GROUP BY reaction ORDER BY COUNT(*) DESC, reaction");
        stmt.bindText(":comment", commentId);
        std::vector<ReactionCount> out;
        while (stmt.step()) out.push_back({stmt.getText(0), stmt.getInt(1)});
        return out;
    });
}

}  // namespace eden
