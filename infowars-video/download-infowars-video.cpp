
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>

#include <json11.hpp>

#include <string>

using namespace std;

const string api_url = "https://api.infowarsmedia.com/api/channel/5b885d33e6646a0015a6fa2d/videos?limit=99&offset=0";

int main(int argc,char **argv) {
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    char timestr[128];

    // Round to half an hour for JS name to avoid hitting their API too often. Be nice.
    sprintf(timestr,"%04u%02u%02u-%02u%02u%02u",
        tm.tm_year+1900,
        tm.tm_mon+1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min - (tm.tm_min % 30),
        0);

    string js_file = string("iw-api-") + timestr + ".js";//WARNING: No spaces allowed!

    {
        struct stat st;

        if (stat(js_file.c_str(),&st) != 0) {
            assert(js_file.find_first_of(" ") == string::npos);
            string cmd = string("wget --show-progress --limit-rate=750K -O ") + js_file + " " + api_url;
            int status = system(cmd.c_str());
            if (status != 0) return 1;
        }
    }

    return 0;
}

