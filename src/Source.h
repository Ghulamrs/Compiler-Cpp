#pragma once

#include <string>
#include <vector>

// Thrown by fail() inside a trial, and caught by whoever began it.
struct SubstitutionFailure {
    std::string why;
    // **A feature this compiler has not built is not a substitution failure.**
    // [temp.deduct]/8 is about types and expressions that are ill-formed, not
    // about a refusal - so a candidate must not be dropped for one, and the
    // site that catches this re-raises when it is set.
    bool unsupported = false;
    std::size_t pos = 0;
};

class Source {
public:
    struct Line {
        int file;
        int line;
    };

    Source(std::string name, std::string text);
    Source(std::string name, std::string text, std::vector<std::string> files,
           std::vector<Line> lines);

    const std::string &text() const { return text_; }
    const char *begin() const { return text_.c_str(); }

    struct Place {
        int file;
        int line;
        int column;
    };
    Place locate(std::size_t pos) const;

    const std::vector<std::string> &files() const { return files_; }

    static Source fromFile(const std::string &path);

    [[noreturn]] void fail(std::size_t pos, const std::string &message) const;

// **A trial: a stretch of parsing whose failure is an answer.** [temp.deduct]/8
// drops the specialization and issues no diagnostic. The one place here where a
// failure must not stop the compiler; everywhere else an error stops it.
    void beginTrial() const { trials_++; }
    void endTrial() const { trials_--; }

private:
    mutable int trials_ = 0;
    std::string name_;
    std::string text_;
    std::vector<std::string> files_;
    std::vector<Line> lines_;

    mutable std::vector<std::size_t> lineStarts_;
    void indexLines() const;

    void lineAt(std::size_t pos, int *line, std::size_t *start) const;
};
