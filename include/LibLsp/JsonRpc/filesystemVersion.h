#if __cplusplus < 201703L

#include <ghc/filesystem.hpp>
namespace filesystem = ghc::filesystem;

#else

#include <filesystem>
namespace filesystem = std::filesystem;

#endif
