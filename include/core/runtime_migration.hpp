#pragma once
#include <filesystem>
#include <string>
namespace wowee::core {
// Rename within the same filesystem; never overwrite either version of a file.
// When both trees exist, retain conflicting old entries in a named backup.
inline bool migrateRuntimeDirectory(const std::filesystem::path& oldPath,
                                    const std::filesystem::path& newPath,
                                    std::string& error) {
    namespace fs=std::filesystem;
    std::error_code ec;
    const auto oldStatus=fs::symlink_status(oldPath,ec);
    if(ec==std::errc::no_such_file_or_directory || oldStatus.type()==fs::file_type::not_found)return true;
    if(ec){error=ec.message();return false;}
    if(!fs::is_directory(oldStatus)){error="Legacy runtime path is not a directory";return false;}
    const bool present=fs::exists(newPath,ec);
    if(ec){error=ec.message();return false;}
    if(!present){fs::rename(oldPath,newPath,ec);if(ec){error=ec.message();return false;}return true;}
    if(!fs::is_directory(fs::symlink_status(newPath,ec)) || ec){error="Runtime destination is not a directory";return false;}
    // Increment the iterator before moving its current entry. Symlinks are
    // moved as entries; they are never followed out of the runtime directory.
    fs::recursive_directory_iterator it(oldPath,ec),end;
    while(!ec && it!=end){
        const auto path=it->path();const auto status=it->symlink_status(ec);if(ec)break;
        const auto dest=newPath/path.lexically_relative(oldPath);
        if(fs::is_directory(status)){
            const auto destStatus=fs::symlink_status(dest,ec);
            if(ec==std::errc::no_such_file_or_directory)ec.clear();
            if(ec)break;
            if(destStatus.type()==fs::file_type::not_found){fs::create_directory(dest,ec);if(ec)break;}
            else if(!fs::is_directory(destStatus))it.disable_recursion_pending();
            it.increment(ec);continue;
        }
        it.increment(ec);if(ec)break;
        const auto destination=fs::symlink_status(dest,ec);
        if(ec==std::errc::no_such_file_or_directory)ec.clear();
        if(ec)break;
        if(destination.type()==fs::file_type::not_found){fs::rename(path,dest,ec);if(ec)break;}
    }
    if(ec){error=ec.message();return false;}
    auto backup=fs::path(newPath.string()+"_legacy_backup");
    for(unsigned n=2;fs::exists(backup,ec) && !ec;++n)backup=newPath.string()+"_legacy_backup_"+std::to_string(n);
    if(!ec)fs::rename(oldPath,backup,ec);
    if(ec){error=ec.message();return false;}
    return true;
}
}
