#include "ntp_time.h"
#include "secrets.h"
#include <time.h>

static bool synced = false;
static String lastSync = "never";

void ntp_init() {
    configTzTime(TZ_POSIX, NTP_SERVER);
    Serial.println("[NTP ] Configured");
}

bool ntp_synced() {
    if (!synced) {
        struct tm t;
        if (getLocalTime(&t, 0)) {
            synced = true;
            char buf[9];
            strftime(buf, sizeof(buf), "%H:%M:%S", &t);
            lastSync = String(buf);
            Serial.printf("[NTP ] Synced: %s\n", buf);
        }
    }
    return synced;
}

String ntp_time_str() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return "--:--:--";
    char buf[9];
    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    return String(buf);
}

String ntp_date_str() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return "---";

    static const char *dager[] = {"Son", "Man", "Tir", "Ons", "Tor", "Fre", "Lor"};
    static const char *mnd[] = {"jan", "feb", "mar", "apr", "mai", "jun",
                                "jul", "aug", "sep", "okt", "nov", "des"};
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d. %s", dager[t.tm_wday], t.tm_mday, mnd[t.tm_mon]);
    return String(buf);
}

String ntp_last_sync() { return lastSync; }
