#pragma once

#include "backend/Backend.h"

#include <string>
#include <vector>

class Driver {
public:
    int run(int argc, char **argv);

private:
    struct Job {
        std::string input;
        std::string output;
    };

    std::string program_;
    std::vector<Job> jobs_;
    std::vector<std::string> searchPath_;
    const Backend *backend_ = &defaultBackend();
    bool toStdout_ = false;
    bool timing_ = false;
    bool assemblyOnly_ = false;
    bool debug_ = false;
    bool objectOnly_ = false;
    unsigned threads_ = 0;
    // **Which assembler the Windows target is written for.** It lived as a
    // file-scope bool in the backend, which made it one object shared by every
    // compiling thread; it is an option of this invocation, so it belongs
    // here beside the others and is handed to the backend at the call.
    bool gnuAsm_ = false;
    std::string linkTo_;
    std::vector<std::string> temporaries_;
    std::vector<std::string> objects_;

    struct MacroEdit {
        std::string name;
        std::string value;
        bool undef;
    };
    std::vector<MacroEdit> macroEdits_;

    bool parseArguments(int argc, char **argv);

    bool compile(const Job &job);

    bool runJobs();
    unsigned threadCount() const;
    unsigned threadCount(std::size_t items) const;
    // Run a batch of tool invocations on that many threads, reporting the
    // first failure with the command that produced it.
    bool runCommands(const std::vector<std::string> &commands);
    bool link();
    bool assembleObjects();
    void removeTemporaries();
    std::vector<std::pair<std::string, std::string> > macrosFor() const;
    void addMacroEdit(const char *text, bool undef);

    static unsigned availableCores();

    static std::string assemblyNameFor(const std::string &source);
    static std::string objectNameFor(const std::string &source);
    static std::string temporaryName(int index);
    static const char *hostCompiler();
    static const char *hostAssembler();
    // The assembler for the GNU spelling of x86_64-windows, which is a
    // different program from ml64 rather than the same one with a flag.
    static const char *hostGnuAssembler();
    static const char *hostLinker();
    static void usage(char *);
};
