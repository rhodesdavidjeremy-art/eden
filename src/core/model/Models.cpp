#include "model/Models.hpp"

#include "common/Strings.hpp"

namespace eden {

const char* toString(FileType value) {
    switch (value) {
        case FileType::Pdf: return "pdf";
        case FileType::Docx: return "docx";
        case FileType::Txt: return "txt";
        case FileType::Epub: return "epub";
        case FileType::Eden: return "eden";
        case FileType::Unknown: break;
    }
    return "unknown";
}

const char* toString(PackOrigin value) {
    switch (value) {
        case PackOrigin::Manual: return "manual";
        case PackOrigin::Auto: return "auto";
        case PackOrigin::Author: return "author";
    }
    return "manual";
}

const char* toString(CommentKind value) {
    switch (value) {
        case CommentKind::Text: return "text";
        case CommentKind::Image: return "image";
        case CommentKind::Video: return "video";
        case CommentKind::Voice: return "voice";
        case CommentKind::Camera: return "camera";
    }
    return "text";
}

const char* toString(SourceTrust value) {
    switch (value) {
        case SourceTrust::Unrated: return "unrated";
        case SourceTrust::Low: return "low";
        case SourceTrust::Medium: return "medium";
        case SourceTrust::High: return "high";
    }
    return "unrated";
}

const char* toString(RuleKind value) {
    return value == RuleKind::Allow ? "allow" : "block";
}

const char* toString(SessionStatus value) {
    switch (value) {
        case SessionStatus::Ok: return "ok";
        case SessionStatus::Error: return "error";
        case SessionStatus::Cancelled: return "cancelled";
    }
    return "ok";
}

FileType fileTypeFromString(const std::string& text) {
    const std::string key = toLowerAscii(text);
    if (key == "pdf") return FileType::Pdf;
    if (key == "docx") return FileType::Docx;
    if (key == "txt") return FileType::Txt;
    if (key == "epub") return FileType::Epub;
    if (key == "eden") return FileType::Eden;
    return FileType::Unknown;
}

PackOrigin packOriginFromString(const std::string& text) {
    const std::string key = toLowerAscii(text);
    if (key == "auto") return PackOrigin::Auto;
    if (key == "author") return PackOrigin::Author;
    return PackOrigin::Manual;
}

CommentKind commentKindFromString(const std::string& text) {
    const std::string key = toLowerAscii(text);
    if (key == "image") return CommentKind::Image;
    if (key == "video") return CommentKind::Video;
    if (key == "voice") return CommentKind::Voice;
    if (key == "camera") return CommentKind::Camera;
    return CommentKind::Text;
}

SourceTrust sourceTrustFromString(const std::string& text) {
    const std::string key = toLowerAscii(text);
    if (key == "low") return SourceTrust::Low;
    if (key == "medium") return SourceTrust::Medium;
    if (key == "high") return SourceTrust::High;
    return SourceTrust::Unrated;
}

RuleKind ruleKindFromString(const std::string& text) {
    return toLowerAscii(text) == "block" ? RuleKind::Block : RuleKind::Allow;
}

SessionStatus sessionStatusFromString(const std::string& text) {
    const std::string key = toLowerAscii(text);
    if (key == "error") return SessionStatus::Error;
    if (key == "cancelled") return SessionStatus::Cancelled;
    return SessionStatus::Ok;
}

FileType fileTypeFromPath(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return FileType::Unknown;
    return fileTypeFromString(path.substr(dot + 1));
}

}  // namespace eden
