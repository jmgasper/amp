#include "Scanner.h"
#include "ImageCache.h"
#include "Library.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>

#include <taglib/aifffile.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/mp4file.h>
#include <taglib/mp4properties.h>
#include <taglib/wavfile.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tvariant.h>

namespace amp {

namespace {

const char* kExtensions[] = {".mp3", ".mp4", ".m4a", ".m4b", ".aac", ".flac", ".wav", ".wave", ".ogg", ".oga", ".opus", nullptr};
const char* kFolderArt[] = {"cover.jpg", "cover.jpeg", "cover.png", "folder.jpg", "folder.png", "front.jpg", "front.png",
    "album.jpg", "album.png", "Cover.jpg", "Folder.jpg", "artwork.jpg", "artwork.png", nullptr};

std::string Extension(const std::string& path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos)
        return "";
    return ToLower(path.substr(dot));
}

int ParseLeadingInt(const std::string& text)
{
    return atoi(text.c_str());
}

std::string FirstProperty(const TagLib::PropertyMap& props, const char* key)
{
    auto it = props.find(key);
    if (it == props.end() || it->second.isEmpty())
        return "";
    return it->second.front().to8Bit(true);
}

std::string ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return "";
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

Scanner::Scanner(Library& library, ImageCache& images)
    : fLibrary(library), fImages(images)
{
}

Scanner::~Scanner()
{
    Stop();
}

bool Scanner::IsAudioFile(const std::string& path)
{
    std::string ext = Extension(path);
    for (int i = 0; kExtensions[i]; i++)
        if (ext == kExtensions[i])
            return true;
    return false;
}

bool Scanner::ReadTrack(const std::string& path, Track& track, std::string* embeddedArt, std::string* embeddedMime)
{
    if (!IsAudioFile(path))
        return false;
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
    track.source = Source::Local;
    track.uri = path;
    track.sizeBytes = st.st_size;
    track.modifiedTime = st.st_mtime;
    TagLib::FileRef file(path.c_str(), true, TagLib::AudioProperties::Fast);
    if (file.isNull()) {
        // still list the file by name so the user sees it
        size_t slash = path.rfind('/');
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        size_t dot = name.rfind('.');
        track.title = dot == std::string::npos ? name : name.substr(0, dot);
        return true;
    }
    TagLib::Tag* tag = file.tag();
    if (tag) {
        track.title = tag->title().to8Bit(true);
        track.artist = tag->artist().to8Bit(true);
        track.album = tag->album().to8Bit(true);
        track.genre = tag->genre().to8Bit(true);
        track.year = (int)tag->year();
        track.trackNumber = (int)tag->track();
    }
    TagLib::PropertyMap props = file.properties();
    track.albumArtist = FirstProperty(props, "ALBUMARTIST");
    std::string disc = FirstProperty(props, "DISCNUMBER");
    if (!disc.empty())
        track.discNumber = ParseLeadingInt(disc);
    if (track.trackNumber == 0) {
        std::string number = FirstProperty(props, "TRACKNUMBER");
        if (!number.empty())
            track.trackNumber = ParseLeadingInt(number);
    }
    if (track.year == 0) {
        std::string date = FirstProperty(props, "DATE");
        if (date.empty())
            date = FirstProperty(props, "ORIGINALDATE");
        if (!date.empty())
            track.year = ParseLeadingInt(date);
    }
    if (TagLib::AudioProperties* audio = file.audioProperties()) {
        track.durationMs = audio->lengthInMilliseconds();
        track.bitrate = audio->bitrate();
    }
    if (TagLib::File* raw = file.file()) {
        if (dynamic_cast<TagLib::FLAC::File*>(raw) || dynamic_cast<TagLib::RIFF::WAV::File*>(raw)
            || dynamic_cast<TagLib::RIFF::AIFF::File*>(raw))
            track.lossless = true;
        else if (TagLib::MP4::File* mp4 = dynamic_cast<TagLib::MP4::File*>(raw)) {
            TagLib::MP4::Properties* properties = mp4->audioProperties();
            track.lossless = properties && properties->codec() == TagLib::MP4::Properties::ALAC;
        }
    }
    if (track.bitrate <= 0 && track.durationMs > 0)
        track.bitrate = (int)(track.sizeBytes * 8 / track.durationMs); // bytes*8/ms == kbit/s
    if (track.title.empty()) {
        size_t slash = path.rfind('/');
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        size_t dot = name.rfind('.');
        track.title = dot == std::string::npos ? name : name.substr(0, dot);
        // "01 Title" / "01 - Title" file names carry the track number
        size_t digits = 0;
        while (digits < track.title.size() && isdigit((unsigned char)track.title[digits]))
            digits++;
        if (digits > 0 && digits <= 3 && digits < track.title.size()) {
            size_t rest = digits;
            while (rest < track.title.size() && (track.title[rest] == ' ' || track.title[rest] == '-' || track.title[rest] == '.' || track.title[rest] == '_'))
                rest++;
            if (rest < track.title.size()) {
                if (track.trackNumber == 0)
                    track.trackNumber = atoi(track.title.substr(0, digits).c_str());
                track.title = track.title.substr(rest);
            }
        }
    }
    if (track.album.empty() || track.artist.empty()) {
        // untagged formats (raw AAC/ADTS, plain WAV): use "Artist/Album/file" folder names
        size_t slash = path.rfind('/');
        if (slash != std::string::npos) {
            std::string folder = path.substr(0, slash);
            size_t parent = folder.rfind('/');
            std::string albumName = parent == std::string::npos ? folder : folder.substr(parent + 1);
            std::string artistName;
            if (parent != std::string::npos) {
                std::string grand = folder.substr(0, parent);
                size_t gp = grand.rfind('/');
                artistName = gp == std::string::npos ? grand : grand.substr(gp + 1);
            }
            if (track.album.empty() && !albumName.empty())
                track.album = albumName;
            if (track.artist.empty() && !artistName.empty() && artistName != "home" && artistName != "Music")
                track.artist = artistName;
        }
    }
    if (embeddedArt) {
        TagLib::List<TagLib::VariantMap> pictures = file.complexProperties("PICTURE");
        for (const TagLib::VariantMap& picture : pictures) {
            auto data = picture.find("data");
            if (data == picture.end())
                continue;
            TagLib::ByteVector bytes = data->second.toByteVector();
            if (bytes.isEmpty())
                continue;
            embeddedArt->assign(bytes.data(), bytes.size());
            auto mime = picture.find("mimeType");
            if (embeddedMime && mime != picture.end())
                *embeddedMime = mime->second.toString().to8Bit(true);
            auto type = picture.find("pictureType");
            if (type != picture.end() && type->second.toString() == "Front Cover")
                break; // prefer the front cover, otherwise keep the first picture
        }
    }
    return true;
}

void Scanner::Start(const std::vector<std::string>& folders, bool force)
{
    Stop();
    fStop = false;
    fRunning = true;
    fThread = std::thread([this, folders, force] { Run(folders, force); });
}

void Scanner::Stop()
{
    fStop = true;
    if (fThread.joinable())
        fThread.join();
    fRunning = false;
}

void Scanner::Walk(const std::string& folder, std::vector<std::string>& files)
{
    DIR* dir = opendir(folder.c_str());
    if (!dir)
        return;
    std::vector<std::string> subfolders;
    while (struct dirent* entry = readdir(dir)) {
        if (fStop)
            break;
        std::string name = entry->d_name;
        if (name == "." || name == ".." || name.empty() || name[0] == '.')
            continue;
        std::string path = folder + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            subfolders.push_back(path);
        else if (S_ISREG(st.st_mode) && IsAudioFile(path))
            files.push_back(path);
    }
    closedir(dir);
    std::sort(subfolders.begin(), subfolders.end());
    for (const std::string& sub : subfolders)
        Walk(sub, files);
}

void Scanner::Run(std::vector<std::string> folders, bool force)
{
    if (onProgress)
        onProgress("Scanning folders", false);
    std::vector<std::string> files;
    for (const std::string& folder : folders) {
        std::string clean = folder;
        while (clean.size() > 1 && clean.back() == '/')
            clean.pop_back();
        Walk(clean, files);
    }
    std::map<std::string, std::pair<int64_t, int64_t>> known = fLibrary.LocalFileIndex();
    std::map<std::string, bool> present;
    std::vector<Track> batch;
    std::map<std::string, std::string> folderArtChecked; // folder -> art key ("" when none)
    int processed = 0;
    int added = 0;
    for (const std::string& path : files) {
        if (fStop)
            break;
        present[path] = true;
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            continue;
        auto it = known.find(path);
        if (!force && it != known.end() && it->second.first == st.st_mtime && it->second.second == st.st_size) {
            processed++;
            continue;
        }
        Track track;
        std::string art, mime;
        if (!ReadTrack(path, track, &art, &mime))
            continue;
        if (track.durationMs <= 0 && durationProbe) {
            track.durationMs = durationProbe(path);
            if (track.bitrate <= 0 && track.durationMs > 0)
                track.bitrate = (int)(track.sizeBytes * 8 / track.durationMs);
        }
        std::string artist = track.groupingArtist();
        ArtKey key = MakeAlbumArtKey(artist.empty() ? "Unknown Artist" : artist, track.album.empty() ? "Unknown Album" : track.album);
        track.art = key;
        if (!art.empty()) {
            if (!fImages.Has(key))
                fImages.Store(key, art, mime);
        } else {
            std::string folder = path.substr(0, path.rfind('/'));
            auto checked = folderArtChecked.find(folder);
            if (checked == folderArtChecked.end()) {
                std::string found;
                for (int i = 0; kFolderArt[i]; i++) {
                    std::string candidate = folder + "/" + kFolderArt[i];
                    struct stat artStat;
                    if (stat(candidate.c_str(), &artStat) == 0 && artStat.st_size > 0) {
                        found = candidate;
                        break;
                    }
                }
                folderArtChecked[folder] = found;
                checked = folderArtChecked.find(folder);
            }
            if (!checked->second.empty() && !fImages.Has(key)) {
                std::string bytes = ReadFile(checked->second);
                if (!bytes.empty())
                    fImages.Store(key, bytes, Extension(checked->second) == ".png" ? "image/png" : "image/jpeg");
            }
        }
        batch.push_back(track);
        processed++;
        added++;
        if (batch.size() >= 40) {
            fLibrary.UpsertTracks(batch, true);
            batch.clear();
            if (onProgress)
                onProgress("Scanning: " + std::to_string(processed) + " of " + std::to_string(files.size()) + " files", false);
        }
    }
    if (!batch.empty())
        fLibrary.UpsertTracks(batch, false);
    // remove files that disappeared (only when the scan was not interrupted)
    std::vector<std::string> missing;
    if (!fStop) {
        for (auto& entry : known)
            if (!present.count(entry.first)) {
                bool underFolder = false;
                for (const std::string& folder : folders)
                    if (entry.first.compare(0, folder.size(), folder) == 0)
                        underFolder = true;
                if (underFolder)
                    missing.push_back(entry.first);
            }
    }
    if (!missing.empty())
        fLibrary.RemoveTracksByUri(missing, false);
    fLibrary.RebuildIndex();
    fRunning = false;
    if (onProgress)
        onProgress(added || missing.size() ? "Scan finished: " + std::to_string(added) + " new or changed, "
            + std::to_string(missing.size()) + " removed" : "Library up to date", true);
}

} // namespace amp
