// scantool: runs Amp's library scanner over folders and reports how long it took.
//   scantool [-j workers] [-k] folder...
// The library and the artwork go to a scratch folder that is removed afterwards (-k keeps it
// and prints where it is), so the tool never touches the library Amp itself uses.
#include "core/ImageCache.h"
#include "core/Library.h"
#include "core/Scanner.h"
#include "core/Settings.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace amp;

int main(int argc, char** argv)
{
    int workers = 0;
    bool keep = false;
    std::vector<std::string> folders;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-j") == 0 && i + 1 < argc)
            workers = atoi(argv[++i]);
        else if (strcmp(argv[i], "-k") == 0)
            keep = true;
        else
            folders.push_back(argv[i]);
    }
    if (folders.empty()) {
        fprintf(stderr, "usage: scantool [-j workers] [-k] folder...\n");
        return 1;
    }
    const char* base = getenv("TMPDIR");
    std::string scratch = std::string(base && *base ? base : "/tmp") + "/amp-scantool-" + std::to_string((long)getpid());
    mkdir(scratch.c_str(), 0755);
    int result = 0;
    {
        Settings settings(scratch + "/settings.json");
        Library library(scratch + "/library.db");
        std::string error;
        if (!library.Open(error)) {
            fprintf(stderr, "cannot open the scratch library: %s\n", error.c_str());
            return 1;
        }
        ImageCache images(scratch + "/art", settings);
        images.Open();
        Scanner scanner(library, images);
        scanner.SetWorkerCount(workers);
        bool terminal = isatty(fileno(stdout));
        scanner.onProgress = [terminal](const std::string& text, bool done) {
            if (done)
                printf("%s%s\n", terminal ? "\r\033[K" : "", text.c_str());
            else if (terminal)
                printf("\r\033[K%s", text.c_str());
            fflush(stdout);
        };
        for (int pass = 0; pass < 2; pass++) {
            auto start = std::chrono::steady_clock::now();
            scanner.Start(folders, false);
            while (scanner.IsRunning())
                usleep(20000);
            scanner.Stop();
            double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            size_t tracks = library.TrackCount();
            if (pass == 0)
                printf("first scan: %zu songs, %zu albums, %zu artists in %.1f s (%.0f songs/s), artwork %.1f MB\n", tracks,
                    library.AllAlbumIds().size(), library.AllArtistIds().size(), seconds, seconds > 0 ? tracks / seconds : 0.0,
                    images.CacheSizeBytes() / 1048576.0);
            else
                printf("scan of the unchanged folders: %.1f s\n", seconds);
            if (tracks == 0)
                result = 2;
        }
    }
    if (keep)
        printf("kept %s\n", scratch.c_str());
    else if (system(("rm -rf '" + scratch + "'").c_str()) != 0)
        fprintf(stderr, "cannot remove %s\n", scratch.c_str());
    return result;
}
