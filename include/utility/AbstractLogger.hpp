//
// Created by Laura on 7/2/26.
//

#ifndef GISCUPBONN_ABSTRACTLOGGER_HPP
#define GISCUPBONN_ABSTRACTLOGGER_HPP

#include <filesystem>
#include <iostream>
#include <fstream>



// Abstract class for logging.
// Per default logs to std::cout, but can be set to log to any ostream, e.g. std::cerr or a filestream
class Logger {
public:
    Logger();
    Logger(std::ostream &stream);   // Note: caller must ensure the stream outlives this logger
    Logger(std::filesystem::path const &filepath, std::ios::openmode mode = std::ios::out);
    Logger(std::string const &filename, std::ios::openmode mode = std::ios::out);
    virtual ~Logger() = default;

    // Change default stream after construction
    void set_default_stream(std::ostream &stream);
    void set_default_stream(std::filesystem::path const &filepath, std::ios::openmode mode = std::ios::out);
    void set_default_stream(std::string const &filename, std::ios::openmode mode = std::ios::out);

    template<typename... Args>
    void log(Args const &... args) const;

    template<typename... Args>
    void log(std::ostream &stream, Args const &... args) const;

    // ...
    // Write class specific log functions, e.g. for parameter logging, solution logging etc.
    // ...

protected:
    std::ostream &stream() const;

private:
    static std::unique_ptr<std::ofstream> open_log_file(std::filesystem::path const &filepath,
                                                        std::ios::openmode mode = std::ios::out);

    std::unique_ptr<std::ofstream> file;
    mutable std::ostream *default_stream = nullptr;
};


/* -------- Constructors -------- */

inline Logger::Logger()
    : default_stream(&std::cout)
{}

inline Logger::Logger(std::ostream &stream)
    : default_stream(&stream)
{}

inline Logger::Logger(std::string const &filename, std::ios::openmode mode)
    : Logger(std::filesystem::path(filename), mode)
{}

inline Logger::Logger(std::filesystem::path const &filepath, std::ios::openmode mode)
    : file(open_log_file(filepath, mode))
    , default_stream(file.get())
{}


/* -------- set_default_stream -------- */

inline void Logger::set_default_stream(std::ostream &stream) {
    if (not stream) throw std::runtime_error("Logger: provided stream is invalid.");
    file.reset();
    default_stream = &stream;
}

inline void Logger::set_default_stream(std::string const &filename, std::ios::openmode mode) {
    set_default_stream(std::filesystem::path(filename), mode);
}

inline void Logger::set_default_stream(std::filesystem::path const &filepath, std::ios::openmode mode) {
    file = open_log_file(filepath, mode);
    default_stream = file.get();
}


/* -------- log -------- */

template<typename... Args>
void Logger::log(Args const &... args) const {
    (stream() << ... << args) << "\n";
}

template<typename... Args>
void Logger::log(std::ostream &_stream, Args const &... args) const {
    if (not _stream) throw std::runtime_error("Logger: provided stream is invalid.");
    (_stream << ... << args) << "\n";
}


/* -------- other implementations -------- */

inline std::ostream &Logger::stream() const {
    if (not default_stream) throw std::runtime_error("Logger: default_stream is null.");
    if (not *default_stream) throw std::runtime_error("Logger: default stream is in a bad state.");
    return *default_stream;
}


inline std::unique_ptr<std::ofstream> Logger::
open_log_file(std::filesystem::path const &filepath, std::ios::openmode mode)
{
    namespace fs = std::filesystem;
    // Create parent directories if they don't exist
    if (filepath.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(filepath.parent_path(), ec);
        if (ec) {
            throw std::runtime_error("Logger: Could not create directories for '"
                + filepath.string() + "': " + ec.message());
        }
    }
    // Warn to cerr if overwriting — can't use log() yet, stream isn't set up
    if (fs::exists(filepath)) {
        std::cerr << "Logger: Warning: overwriting existing log file '"
                  << filepath.string() << "'\n";
    }
    auto stream = std::make_unique<std::ofstream>(filepath, mode);
    if (not *stream) {
        throw std::runtime_error("Logger: Could not open log file '"
            + filepath.string() + "' for logging.");
    }
    return stream;
}


#endif // GISCUPBONN_ABSTRACTLOGGER_HPP