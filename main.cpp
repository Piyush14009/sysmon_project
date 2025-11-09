// Simple System Monitor Tool (Assignment 3)
// Builds on Linux systems. No external libraries required.
// Compile: g++ -std=c++17 src/main.cpp -o sysmon

#include <bits/stdc++.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

using namespace std;

volatile sig_atomic_t keep_running = 1;
void handle_sigint(int){ keep_running = 0; }

struct CpuTimes {
    unsigned long long user=0, nice=0, system=0, idle=0, iowait=0, irq=0, softirq=0, steal=0;
    unsigned long long total() const {
        return user+nice+system+idle+iowait+irq+softirq+steal;
    }
};

CpuTimes read_cpu_times(){
    CpuTimes t;
    ifstream f("/proc/stat");
    string line;
    if (!f.is_open()) return t;
    while (getline(f, line)){
        if (line.rfind("cpu ", 0) == 0){
            // parse
            stringstream ss(line);
            string cpu;
            ss >> cpu >> t.user >> t.nice >> t.system >> t.idle >> t.iowait >> t.irq >> t.softirq >> t.steal;
            break;
        }
    }
    return t;
}

struct ProcInfo {
    int pid;
    string name;
    unsigned long long utime=0, stime=0;
    unsigned long long total_time() const { return utime + stime; }
    unsigned long long rss = 0; // in pages
    double cpu_percent = 0.0;
    unsigned long long mem_kb = 0;
};

unsigned long long get_uptime_ticks_per_sec(){
    static long v = sysconf(_SC_CLK_TCK);
    return (unsigned long long)v;
}

double parse_meminfo_total_kb(){
    ifstream f("/proc/meminfo");
    string line;
    while (getline(f,line)){
        if (line.rfind("MemTotal:",0)==0){
            string key; unsigned long long kb; string unit;
            stringstream ss(line);
            ss >> key >> kb >> unit;
            return (double)kb;
        }
    }
    return 0.0;
}

vector<int> list_pids(){
    vector<int> pids;
    DIR *dp = opendir("/proc");
    if (!dp) return pids;
    struct dirent *entry;
    while ((entry = readdir(dp)) != nullptr){
        if (entry->d_type == DT_DIR){
            string name(entry->d_name);
            if (all_of(name.begin(), name.end(), ::isdigit)){
                pids.push_back(stoi(name));
            }
        }
    }
    closedir(dp);
    return pids;
}

bool read_proc_stat(int pid, ProcInfo &p){
    string path = "/proc/" + to_string(pid) + "/stat";
    ifstream f(path);
    if (!f.is_open()) return false;
    string content;
    getline(f, content);
    // stat format: pid (comm) state ppid ... utime stime cutime cstime ...
    // comm may contain spaces; find parentheses
    size_t lparen = content.find('(');
    size_t rparen = content.rfind(')');
    if (lparen==string::npos || rparen==string::npos) return false;
    p.name = content.substr(lparen+1, rparen-lparen-1);
    string after = content.substr(rparen+2); // skip ") "
    stringstream ss(after);
    vector<string> fields;
    string tmp;
    while (ss >> tmp) fields.push_back(tmp);
    if (fields.size() < 15) return false;
    // utime is field 13 (index 13-1=12) relative to after
    // But because we skipped upto after rparen, the fields vector starts at state (1), so:
    // fields[11] = utime, fields[12] = stime (0-based)
    try {
        p.utime = stoull(fields[13-2]); // adjust: state is fields[0], so utime index 13-3? safe to search differently
    } catch(...){
        // fallback parse by tokenizing whole line
        stringstream ssAll(content);
        vector<string> all;
        while (ssAll >> tmp) all.push_back(tmp);
        if (all.size() > 14){
            p.utime = stoull(all[13]);
            p.stime = stoull(all[14]);
        } else return false;
    }
    // above handling sometimes off; try robust approach:
    // We'll re-parse using scanning for numeric fields after rparen:
    {
        string s = content.substr(rparen+1);
        stringstream s2(s);
        vector<unsigned long long> nums;
        string tok;
        while (s2 >> tok){
            // if tok is number, push
            bool isn = !tok.empty() && (isdigit(tok[0]) || (tok[0]=='-' && tok.size()>1));
            if (isn){
                try { nums.push_back(stoull(tok)); } catch(...) { nums.push_back(0); }
            }
        }
        if (nums.size() >= 15){
            // utime at index 13 (1-based) -> nums[13-3]? Simpler: from /proc/[pid]/stat fields per man  proc:
            // fields: 1 pid, 2 comm, 3 state, 4 ppid, ..., 14 utime, 15 stime (so offsets in nums considering we've dropped pid and comm)
            // Our nums vector starts from the field right after ')' so its first element is state (field3). So utime is nums[13-3]=nums[10]? Let's compute:
            // state(field3) -> nums[0], so utime(field14) -> nums[14-3]=nums[11]
            if (nums.size() > 11){
                p.utime = nums[11];
                p.stime = nums[12];
            }
        }
    }

    // read RSS from statm or status
    string status_path = "/proc/" + to_string(pid) + "/status";
    ifstream fs(status_path);
    if (fs.is_open()){
        string line;
        while (getline(fs, line)){
            if (line.rfind("VmRSS:",0)==0){
                string key; unsigned long long kb; string unit;
                stringstream ss2(line);
                ss2 >> key >> kb >> unit;
                p.mem_kb = kb;
                break;
            }
        }
    }
    return true;
}

int main(){
    signal(SIGINT, handle_sigint);
    const int display_count = 12;
    const int interval_ms = 2000;
    unsigned long long clk_tck = get_uptime_ticks_per_sec();

    double mem_total_kb = parse_meminfo_total_kb();

    CpuTimes prev_cpu = read_cpu_times();
    unordered_map<int, unsigned long long> prev_proc_time;

    while (keep_running){
        this_thread::sleep_for(chrono::milliseconds(interval_ms));
        CpuTimes cur_cpu = read_cpu_times();
        unsigned long long total_prev = prev_cpu.total();
        unsigned long long total_cur = cur_cpu.total();
        unsigned long long total_delta = (total_cur > total_prev) ? (total_cur - total_prev) : 1;

        double cpu_usage_percent = 0.0;
        unsigned long long work_prev = prev_cpu.user + prev_cpu.nice + prev_cpu.system + prev_cpu.irq + prev_cpu.softirq + prev_cpu.steal;
        unsigned long long work_cur = cur_cpu.user + cur_cpu.nice + cur_cpu.system + cur_cpu.irq + cur_cpu.softirq + cur_cpu.steal;
        unsigned long long work_delta = (work_cur > work_prev) ? (work_cur - work_prev) : 0;
        cpu_usage_percent = 100.0 * (double)work_delta / (double)total_delta;

        // gather processes
        vector<int> pids = list_pids();
        vector<ProcInfo> procs;
        for (int pid : pids){
            ProcInfo p; p.pid = pid;
            if (read_proc_stat(pid, p)){
                unsigned long long prev_pt = prev_proc_time.count(pid) ? prev_proc_time[pid] : 0;
                unsigned long long cur_pt = p.total_time();
                unsigned long long pt_delta = (cur_pt > prev_pt) ? (cur_pt - prev_pt) : 0;
                // process cpu% = 100 * (pt_delta / total_delta)
                double proc_cpu = 100.0 * (double)pt_delta / (double)total_delta;
                p.cpu_percent = proc_cpu;
                procs.push_back(p);
                prev_proc_time[pid] = cur_pt;
            }
        }

        // sort by cpu_percent desc
        sort(procs.begin(), procs.end(), [](const ProcInfo &a, const ProcInfo &b){
            return a.cpu_percent > b.cpu_percent;
        });

        // clear screen and print
        cout << "\033[2J\033[H"; // clear and move cursor home
        cout << "Simple System Monitor - Assignment 3\n";
        cout << "Refresh interval: " << (interval_ms/1000.0) << "s | Approx CPU usage: " << fixed << setprecision(1) << cpu_usage_percent << "%\n";
        cout << "Memory total: " << (unsigned long long)mem_total_kb << " KB\n";
        cout << "PID\t%CPU\tRSS(KB)\tNAME\n";
        int shown = 0;
        for (const auto &p : procs){
            cout << p.pid << "\t" << fixed << setprecision(1) << p.cpu_percent << "\t" << p.mem_kb << "\t" << p.name << "\n";
            if (++shown >= display_count) break;
        }
        cout << "\nPress Ctrl+C to exit\n";

        prev_cpu = cur_cpu;
    }

    cout << "\nExiting sysmon...\n";
    return 0;
}
