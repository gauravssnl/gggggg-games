
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>

#include <json11.hpp>

#include <string>
#include <algorithm>

using namespace std;
using namespace json11;

int                         initial_parts = 50;
int                         part_size = 50;

struct js_entry {
    string      json;

    bool operator==(const struct js_entry &e) const {
        return  json == e.json;
    }
    void clear() {
        json.clear();
    }
};

struct jslist {
    vector<js_entry>        playlist;
    long                    next_part = 0;

    void clear() {
        next_part = 0;
        playlist.clear();
    }
};

char line_tmp[4096];

void chomp(char *s) {
	char *e = s + strlen(s) - 1;
	while (e >= s && (*e == '\n' || *e == '\r')) *e-- = 0;
}

int load_js_list(jslist &jsl,const string &jsfile) {
    jsl.clear();

    FILE *fp = fopen(jsfile.c_str(),"r");
    if (fp == NULL) return -1; // sets errno

    js_entry cur;
    bool cur_set = false;

    while (!feof(fp) && !ferror(fp)) {
        if (fgets(line_tmp,sizeof(line_tmp),fp) == NULL) break;
        chomp(line_tmp);

        if (line_tmp[0] == 0) continue;
        if (line_tmp[0] == '#') {
            const char *s = line_tmp+1; while (*s == ' ') s++;
            string name,value;
            {
                const char *base = s;
                while (*s && *s != ':') s++;
                name = string(base,(size_t)(s-base));
                if (*s == ':') {
                    s++;
                    base = s;
                    while (*s) s++;
                    value = string(base,(size_t)(s-base));
                }
            }

            if (name == "next-part")
                jsl.next_part = atol(value.c_str());

            continue;
        }

        // JSON
        cur.json = line_tmp;
        cur_set = true;

        // emit
        if (cur_set) {
            jsl.playlist.push_back(cur);
            cur_set = false;
            cur = js_entry();
        }
    }

    // emit
    if (cur_set) {
        jsl.playlist.push_back(cur);
        cur_set = false;
        cur = js_entry();
    }

    fclose(fp);
    return 0;
}

int write_js_list(const string &jsfile,jslist &jsl) {
    FILE *fp = fopen(jsfile.c_str(),"w");
    if (fp == NULL) return -1; // sets errno

    if (jsl.next_part != 0)
        fprintf(fp,"# next-part:%ld\n",jsl.next_part);

    for (auto jsi=jsl.playlist.begin();jsi!=jsl.playlist.end();jsi++) {
        auto &jsent = *jsi;
        if (jsent.json.empty()) continue;
        fprintf(fp,"%s\n",jsent.json.c_str());
    }

    fclose(fp);
    return 0;
}

int main(int argc,char **argv) {
    string api_url;
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    char timestr[128];

    if (argc < 2) {
        fprintf(stderr,"Need channel URL\n");
        return 1;
    }
    api_url = argv[1];

    string js_file = "playlist-combined.json"; // use .json not .js so the archive-off script does not touch it
    {
        sprintf(timestr,"%04u%02u%02u-%02u%02u%02u",
            tm.tm_year+1900,
            tm.tm_mon+1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec - (tm.tm_sec % 60)); // TODO: reduce to 15 seconds if confident
    }
    string js_tmp_file = string("playlist-tmp-") + timestr + ".js";
    string js_mix_tmp = "playlist-tmp-combine.js";

    jslist jsl;
    jslist jslnew;

    load_js_list(jsl,js_file);

    {
        struct stat st;

        memset(&st,0,sizeof(st));
        if (stat(js_tmp_file.c_str(),&st) != 0 || st.st_size < 32) {
            assert(js_tmp_file.find_first_of(' ') == string::npos);
            assert(js_tmp_file.find_first_of('$') == string::npos);
            assert(js_tmp_file.find_first_of('\'') == string::npos);
            assert(js_tmp_file.find_first_of('\"') == string::npos);

            assert(api_url.find_first_of(' ') == string::npos);
            assert(api_url.find_first_of('$') == string::npos);
            assert(api_url.find_first_of('\'') == string::npos);
            assert(api_url.find_first_of('\"') == string::npos);

            fprintf(stderr,"Downloading playlist...\n");

            /* -j only emits to stdout, sorry. */
            /* FIXME: Limit to the first 99. Some channels have so many entries that merely querying them causes YouTube
             *        to hit back with "Too many requests".
             *
             *        Speaking of gradual playlist building... the same logic should be implemented into the banned.video
             *        downloader as well. */
            string cmd = string("youtube-dl --no-mtime -j --flat-playlist --playlist-end=99 ") + " \"" + api_url + "\" >" + js_tmp_file;
            int status = system(cmd.c_str());
            if (status != 0) return 1;
        }
    }

    load_js_list(jslnew,js_tmp_file);

    for (auto jslnewi=jslnew.playlist.begin();jslnewi!=jslnew.playlist.end();jslnewi++) {
        if (find(jsl.playlist.begin(),jsl.playlist.end(),*jslnewi) == jsl.playlist.end())
            jsl.playlist.push_back(*jslnewi);
    }

    if (write_js_list(js_mix_tmp,jsl)) {
        fprintf(stderr,"Failed to write JS\n");
        return 1;
    }
    rename(js_mix_tmp.c_str(),js_file.c_str());
    return 0;
}

