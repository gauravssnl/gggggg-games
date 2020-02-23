
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>

#include <json11.hpp>

#include <string>

using namespace std;
using namespace json11;

time_t                      failignore_timeout = 7 * 24 * 60 * 60; // 7 days

int                         youtube_bitrate = 2000;

int                         failignore_mark_counter = 0;

std::string                 youtube_user,youtube_pass;

bool should_stop = false;

void init_marker(void) {
    mkdir("marker",0755);
}

string get_mark_failignore_filename(const string &filename) {
    return string("marker/") + filename + ".fail-ignore";
}

void mark_failignore_file(const string &filename) {
    const string mark_filename = get_mark_failignore_filename(filename);
    int fd = open(mark_filename.c_str(),O_CREAT|O_TRUNC,0644); /* create file, truncate if exists, which updates mtime */
    if (fd >= 0) close(fd);
}

string get_mark_filename(const string &filename) {
    return string("marker/") + filename;
}

void mark_file(const string &filename) {
    const string mark_filename = get_mark_filename(filename);
    int fd = open(mark_filename.c_str(),O_CREAT|O_EXCL,0644);
    if (fd >= 0) close(fd);
}

bool download_video_youtube(const Json &video) {
    string title = video["title"].string_value();
    string url = video["url"].string_value();
    string id = video["id"].string_value();

    if (id.empty() || url.empty()) return false;
    assert(id.find_first_of(' ') == string::npos);
    assert(id.find_first_of('$') == string::npos);
    assert(id.find_first_of('\'') == string::npos);
    assert(id.find_first_of('\"') == string::npos);

    {
        struct stat st;

        string mark_filename = get_mark_filename(id);
        if (stat(mark_filename.c_str(),&st) == 0) {
            return false; // already exists
        }
    }

    {
        time_t now = time(NULL);
        struct stat st;

        string mark_filename = get_mark_failignore_filename(id);
        if (stat(mark_filename.c_str(),&st) == 0) {
            if ((st.st_mtime + failignore_timeout) >= now) {
                fprintf(stderr,"Ignoring '%s', failignore\n",id.c_str());
                return false; // failignore
            }
        }
    }

    sleep(1);

    string tagname = id;
    if (!title.empty()) {
        tagname += "-";
        tagname += title;
    }
    if (tagname != id) {
        if (tagname.length() > 128)
            tagname = tagname.substr(0,128);

        for (auto &c : tagname) {
            if (c < 32 || c > 126 || c == ':' || c == '\\' || c == '/')
                c = ' ';
        }

        tagname += ".txt";

        {
            FILE *fp = fopen(tagname.c_str(),"w");
            if (fp) {
                fclose(fp);
            }
        }
    }

    /* we trust the ID will never need characters that require escaping.
     * they're alphanumeric base64 so far. */
    string invoke_url = string("https://www.youtube.com/watch?v=") + id;

    string creds;

    if (!youtube_user.empty() && !youtube_pass.empty())
        creds += string("--username '") + youtube_user + "' --password '" + youtube_pass + "' ";

    time_t dl_begin = time(NULL);

    {
        string cmd = string("youtube-dl --cookies cookies.txt --no-mtime --continue --all-subs --limit-rate=") + to_string(youtube_bitrate) + "K --output '%(id)s' " + creds + invoke_url;
        int status = system(cmd.c_str());
        if (WIFSIGNALED(status)) should_stop = true;

        if (status != 0) {
            time_t dl_duration = time(NULL) - dl_begin;

            if (dl_duration < 10) {
                fprintf(stderr,"Failed too quickly, marking\n");
                mark_failignore_file(id);
                failignore_mark_counter++;
            }

            return false;
        }
    }

    /* done! */
    mark_file(id);
    return true;
}

bool download_video_bitchute(const Json &video) {
    string title = video["title"].string_value();
    string url = video["url"].string_value();
    string id = video["id"].string_value();

    if (id.empty() || url.empty()) return false;
    assert(id.find_first_of(' ') == string::npos);
    assert(id.find_first_of('$') == string::npos);
    assert(id.find_first_of('\'') == string::npos);
    assert(id.find_first_of('\"') == string::npos);

    {
        struct stat st;

        string mark_filename = get_mark_filename(id);
        if (stat(mark_filename.c_str(),&st) == 0) {
            return false; // already exists
        }
    }

    sleep(1);

    string tagname = id;
    if (!title.empty()) {
        tagname += "-";
        tagname += title;
    }
    if (tagname != id) {
        if (tagname.length() > 128)
            tagname = tagname.substr(0,128);

        for (auto &c : tagname) {
            if (c < 32 || c > 126 || c == ':' || c == '\\' || c == '/')
                c = ' ';
        }

        tagname += ".txt";

        {
            FILE *fp = fopen(tagname.c_str(),"w");
            if (fp) {
                fclose(fp);
            }
        }
    }

    /* we trust the ID will never need characters that require escaping.
     * they're alphanumeric base64 so far. */
    string invoke_url = string("https://www.bitchute.com/video/") + id;

    /* All video on BitChute is .mp4, and youtube-dl needs to be given that suffix */
    {
        string cmd = string("youtube-dl --no-check-certificate --no-mtime --continue --all-subs --limit-rate=2000K --output '%(id)s.mp4' ") + invoke_url;
        int status = system(cmd.c_str());
        if (WIFSIGNALED(status)) should_stop = true;
        if (status != 0) return false;
    }

    /* done! */
    mark_file(id);
    return true;
}

void chomp(char *s) {
    char *e = s + strlen(s) - 1;
    while (e >= s && (*e == '\n' || *e == '\r')) *e-- = 0;
}

int main(int argc,char **argv) {
    string api_url;
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    int download_count = 0;
    int download_limit = 1;
    int failignore_limit = 10;

    if (argc >= 2)
        api_url = argv[1];

    init_marker();

    // per-machine adjustment
    {
        char hostname[256] = {0};
        gethostname(hostname,sizeof(hostname)-1);

        if (!strcmp(hostname,"something")) {
            youtube_bitrate = 500; // Youtube picks on this one more for some reason with "too many requests"
        }
    }

    // youtube creds
    {
        FILE *fp = fopen("youtube-creds.lst","r");
        if (fp == NULL) fp = fopen("../youtube-creds.lst","r");
        if (fp != NULL) {
            char tmp[512];

            fgets(tmp,sizeof(tmp),fp);
            chomp(tmp);
            youtube_user = tmp;

            fgets(tmp,sizeof(tmp),fp);
            chomp(tmp);
            youtube_pass = tmp;

            fclose(fp);
        }

        if (!youtube_user.empty() && !youtube_pass.empty())
            fprintf(stderr,"Using YouTube credentials for '%s'\n",youtube_user.c_str());
    }

    if (strstr(api_url.c_str(),"youtube") != NULL) {
        // not on sunday
        if (tm.tm_wday == 0) {
            fprintf(stderr,"Sunday.\n");
            return 1;
        }
#if 0
        // look human by stopping downloads between 10AM and 4PM
        if (tm.tm_hour >= 10 && tm.tm_hour < (4+12)) {
            fprintf(stderr,"Time for work.\n");
            return 1;
        }
#endif
        // look human by stopping downloads between 12AM and 6AM
        if (tm.tm_hour >= 0 && tm.tm_hour < 6) {
            fprintf(stderr,"Time for bed.\n");
            return 1;
        }
    }

    // backwards compat
    if (!api_url.empty()) {
        assert(api_url.find_first_of(' ') == string::npos);
        assert(api_url.find_first_of('$') == string::npos);
        assert(api_url.find_first_of('\'') == string::npos);
        assert(api_url.find_first_of('\"') == string::npos);
        assert(api_url.find_first_of('\\') == string::npos);

        string cmd = string("ytmon-update-pl ") + api_url;
        int status = system(cmd.c_str());
        if (WIFSIGNALED(status)) return 1;
    }

    // new rule: we no longer download the playlist ourself.
    //           you must run a playlist downloading tool to generate the list this
    //           tool downloads by. The playlist downloading tool can combine the
    //           URLs from the first N videos as well as gradually query the playlist
    //           piecemeal to gather the entire channel list without hitting YouTube's
    //           "Too many requests" condition.
    string js_file = "playlist-combined.json"; // use .json not .js so the archive-off script does not touch it

    /* the JS file is actually MANY JS objects encoded on a line by line basis */

    {
        char buf[4096]; /* should be large enough for now */
        FILE *fp = fopen(js_file.c_str(),"r");
        if (fp == NULL) {
            fprintf(stderr,"Unable to open %s. Please run playlist downloader tool to generate/update it.\n",js_file.c_str());
            return 1;
        }

        memset(buf,0,sizeof(buf));
        while (!feof(fp) && !ferror(fp)) {
            /* Linux: fgets() will terminate the buffer with \0 (NULL) */
            if (fgets(buf,sizeof(buf),fp) == NULL) break;

            /* eat the newline */
            {
                char *s = buf + strlen(buf) - 1;
                while (s > buf && (*s == '\n' || *s == '\r')) *s-- = 0;
            }

            if (buf[0] == 0) continue;
            if (buf[0] == '#') continue; // download aggregator will use # lines for comments and status

            string json_err;
            Json json = Json::parse(buf,json_err);

            if (json == Json()) {
                fprintf(stderr,"JSON parse error: %s\n",json_err.c_str());
                continue;
            }

            if (should_stop)
                break;

            /* YouTube example:
             *
             * {"url": "AbH3pJnFgY8", "_type": "url", "ie_key": "Youtube", "id": "AbH3pJnFgY8", "title": "No More Twitter? \ud83d\ude02"} */
            if (json["ie_key"].string_value() == "Youtube") { /* FIXME: what if youtube-dl changes that? */
                if (download_video_youtube(json)) {
                    if (++download_count >= download_limit)
                        break;

                    sleep(5 + ((unsigned int)rand() % 5u));
                }
            }
            /* Bitchute example:
             *
             * {"url": "https://www.bitchute.com/video/F0yUrwVz9fYV", "_type": "url", "ie_key": "BitChute", "id": "F0yUrwVz9fYV"} */
            else if (json["ie_key"].string_value() == "BitChute") { /* FIXME: what if youtube-dl changes that? */
                if (download_video_bitchute(json)) {
                    if (++download_count >= download_limit)
                        break;

                    sleep(5 + ((unsigned int)rand() % 5u));
                }
            }

            if (failignore_mark_counter >= failignore_limit)
                break;

            if (should_stop)
                break;
        }

        fclose(fp);
    }

    return 0;
}

