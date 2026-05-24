#include <unistd.h>
#include <sys/types.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <Lines/DaemonHost.hpp>
#include <Lines/Bind.hpp>
#include <Resource/File.hpp>
#include <Encoding/Yaml.hpp>

using namespace Rho;
using namespace Lines;
using namespace Resource;
using namespace Encoding;
using namespace Collection;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
inline String getStr(TreeItem* node, const String& defaultVal = "") {
  if (!node) return defaultVal;
  if (auto s = dynamic_cast<const TreeItemT<String> *>(node)) return s->value;
  return defaultVal;
}

inline bool getBool(TreeItem* node, bool defaultVal = false) {
  if (!node) return defaultVal;
  if (auto b = dynamic_cast<const TreeItemT<bool> *>(node)) return b->value;
  return defaultVal;
}

inline String toHex(const String& bin) {
  const char* chars = "0123456789abcdef";
  String hex;
  hex.allocate(bin.size() * 2);
  for (usz i = 0; i < bin.size(); ++i) {
    u8 b = (u8)bin[i];
    hex.push(chars[b >> 4]);
    hex.push(chars[b & 0xf]);
  }
  return hex;
}

inline String fromHex(const String& hex) {
  String bin;
  auto hexVal = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  for (usz i = 0; i < hex.size(); i += 2) {
    if (i + 1 < hex.size()) {
      u8 b = (u8)((hexVal(hex[i]) << 4) | hexVal(hex[i + 1]));
      bin.push(b);
    }
  }
  return bin;
}

inline Resource::NumericalAddress parseNumericalAddress(const String& str) {
  Resource::NumericalAddress addr;
  if (str.isEmpty()) return addr;
  Array<String> parts = str.split(".");
  for (usz i = 0; i < parts.size(); ++i) {
    addr.push((u64)parts[i].toInt());
  }
  return addr;
}

inline String numericalAddressToString(const Resource::NumericalAddress& addr) {
  String str;
  for (usz i = 0; i < addr.size(); ++i) {
    if (i > 0) str += ".";
    str += String(addr[i]);
  }
  return str;
}

inline String getLinesDir() {
  const char* env = getenv("LINES");
  if (env) return env;
  return "/run/lines";
}

inline bool processExists(u32 pid) {
  if (pid == 0) return false;
  return kill((pid_t)pid, 0) == 0;
}

inline u32 readPidFile(const String& linesDir) {
  String pidPath = linesDir + "/pid";
  auto& fs = FilesystemDevice::fs();
  String content = fs.read(pidPath);
  if (content.isEmpty()) return 0;
  return (u32)content.toInt();
}

inline void writePidFile(const String& linesDir, u32 pid) {
  String pidPath = linesDir + "/pid";
  FilesystemDevice::fs().write(pidPath, String((long long)pid));
}

// ---------------------------------------------------------------------------
// Daemon logic
// ---------------------------------------------------------------------------
void runDaemon(const String& linesDir) {
  printf("=========================================\n");
  printf("       Starting Rho Daemon (rhod)        \n");
  printf("=========================================\n");
  printf("Lines directory: %s\n", linesDir.c_str());

  auto& fs = FilesystemDevice::fs();

  // Create directory structure
  fs.mkdir(linesDir);
  fs.mkdir(linesDir + "/ports");
  fs.mkdir(linesDir + "/hook");
  fs.mkdir(linesDir + "/upgrade");
  fs.mkdir(linesDir + "/clients");
  fs.mkdir(linesDir + "/routes");
  fs.mkdir(linesDir + "/hostAs");

  // Write PID
  writePidFile(linesDir, (u32)getpid());

  DaemonHost lh;
  lh.keypair = Sec::generateKeyPair();

  // Load upgrade configs
  {
    String upgradeDir = linesDir + "/upgrade";
    Stat upgradeStat = fs.stat(upgradeDir, 1, 100);
    for (usz i = 0; i < upgradeStat.children.size(); ++i) {
      auto& child = upgradeStat.children[i];
      if (child.isFile && child.path.endsWith(".yml")) {
        String content = fs.read(child.path);
        TaggedTreeBranch configRoot;
        if (parseYAML(content, configRoot)) {
          GatewayInfo gi;
          gi.name = getStr(configRoot.get("name"));
          gi.password = getStr(configRoot.get("password"));
          gi.theirPublicKey = fromHex(getStr(configRoot.get("public")));
          gi.address = parseNumericalAddress(getStr(configRoot.get("address")));

          String identHex = getStr(configRoot.get("identity"));
          if (!identHex.isEmpty()) {
            gi.keypair.secretKey = fromHex(identHex);
            // Derive public from secret if crypto supports it
          }

          lh.upgrades.push(gi);
          printf("[LINES] Loaded upgrade config: %s\n", gi.name.c_str());
        }
      }
    }
  }

  // Load hostAs configs
  {
    String hostAsDir = linesDir + "/hostAs";
    Stat hostAsStat = fs.stat(hostAsDir, 1, 100);
    for (usz i = 0; i < hostAsStat.children.size(); ++i) {
      auto& child = hostAsStat.children[i];
      if (child.isFile && child.path.endsWith(".yml")) {
        String content = fs.read(child.path);
        TaggedTreeBranch configRoot;
        if (parseYAML(content, configRoot)) {
          GatewayInfo gi;
          gi.name = getStr(configRoot.get("name"));
          gi.password = getStr(configRoot.get("password"));

          String addrStr = getStr(configRoot.get("address"));
          gi.address = parseNumericalAddress(addrStr);

          lh.hostAs.push(gi);
          printf("[LINES] Loaded hostAs config: %s\n", gi.name.c_str());
        }
      }
    }
  }

  // Load routes
  {
    String routesDir = linesDir + "/routes";
    Stat routesStat = fs.stat(routesDir, 1, 100);
    for (usz i = 0; i < routesStat.children.size(); ++i) {
      auto& child = routesStat.children[i];
      if (child.isFile && child.path.endsWith(".yml")) {
        String content = fs.read(child.path);
        TaggedTreeBranch configRoot;
        if (parseYAML(content, configRoot)) {
          if (auto routesBranch = dynamic_cast<TreeBranch*>(configRoot.get("routes"))) {
            for (usz k = 0; k < routesBranch->size(); ++k) {
              if (auto entry = dynamic_cast<TreeBranch*>((*routesBranch)[k])) {
                IdentityClaim ic;
                ic.publicKey = fromHex(getStr(entry->get("publicKey")));
                ic.address = parseNumericalAddress(getStr(entry->get("address")));
                lh.routes.push(ic);
              }
            }
          }
          printf("[LINES] Loaded routes from: %s\n", child.path.c_str());
        }
      }
    }
  }

  // Set up hook stations from hook/ directory
  {
    String hookDir = linesDir + "/hook";
    Stat hookStat = fs.stat(hookDir, 1, 100);
    for (usz i = 0; i < hookStat.children.size(); ++i) {
      auto& child = hookStat.children[i];
      if (child.isSocket) {
        String hookPath = child.path;
        FileBind* fb = new FileBind(hookPath);
        lh.hook(*fb);
        printf("[LINES] Hooked to socket: %s\n", hookPath.c_str());
      }
    }
  }

  // Set up port stations from ports/ directory
  {
    String portsDir = linesDir + "/ports";
    Stat portsStat = fs.stat(portsDir, 1, 100);
    for (usz i = 0; i < portsStat.children.size(); ++i) {
      auto& child = portsStat.children[i];
      String baseName = Resource::Path(child.path).basename();
      // Check if the basename is a parseable integer
      bool isInt = !baseName.isEmpty();
      for (usz j = 0; j < baseName.size() && isInt; ++j) {
        if (baseName[j] < '0' || baseName[j] > '9') isInt = false;
      }
      if (isInt) {
        u32 port = (u32)baseName.toInt();
        lh.bind(port);
        printf("[LINES] Bound port: %u\n", port);
      }
    }
  }

  // Main loop
  printf("[LINES] Daemon running (PID %d)\n", (int)getpid());
  while (true) {
    lh.update();
    usleep(10000); // 10ms tick
  }
}

// ---------------------------------------------------------------------------
// CLI commands
// ---------------------------------------------------------------------------
void ensureDaemon(const String& linesDir) {
  u32 existingPid = readPidFile(linesDir);
  if (existingPid > 0 && processExists(existingPid)) {
    return; // Daemon already running
  }
  // Need to start a new daemon
  printf("[LINES] No running daemon found. Starting one...\n");
  pid_t pid = fork();
  if (pid == 0) {
    // Child: become daemon
    setsid();
    runDaemon(linesDir);
    _exit(0);
  }
  // Parent: wait a moment for daemon to start
  usleep(100000);
}

void cmdStart(const String& linesDir) {
  u32 existingPid = readPidFile(linesDir);
  if (existingPid > 0 && processExists(existingPid)) {
    printf("[LINES] Daemon already running (PID %u)\n", existingPid);
    return;
  }
  pid_t pid = fork();
  if (pid == 0) {
    setsid();
    // Close stdin/stdout/stderr for true daemonization
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    runDaemon(linesDir);
    _exit(0);
  } else if (pid > 0) {
    printf("[LINES] Daemon forked (PID %d)\n", pid);
  } else {
    printf("[LINES] Failed to fork daemon\n");
  }
}

void cmdDaemon(const String& linesDir) {
  // Run daemon in foreground
  runDaemon(linesDir);
}

void cmdUpgrade(const String& linesDir, int argc, char** argv) {
  if (argc < 3) {
    printf("Usage: rhod upgrade <Name> [--password=<pass>] [--public=<key>] [--identity=<key>]\n");
    return;
  }

  ensureDaemon(linesDir);

  String name = argv[2];
  String password, publicKey, identity;

  for (int i = 3; i < argc; ++i) {
    String arg = argv[i];
    if (arg.startsWith("--password=")) {
      password = arg.substring(11);
    } else if (arg.startsWith("--public=")) {
      publicKey = arg.substring(9);
    } else if (arg.startsWith("--identity=")) {
      identity = arg.substring(11);
    }
  }

  auto& fs = FilesystemDevice::fs();
  String upgradePath = linesDir + "/upgrade/" + name + ".yml";

  TaggedTreeBranch configRoot;
  configRoot.add(new TaggedTreeItemT<String>(name))->setName("name");
  if (!password.isEmpty()) {
    configRoot.add(new TaggedTreeItemT<String>(password))->setName("password");
  }
  if (!publicKey.isEmpty()) {
    configRoot.add(new TaggedTreeItemT<String>(publicKey))->setName("public");
  }
  if (!identity.isEmpty()) {
    configRoot.add(new TaggedTreeItemT<String>(identity))->setName("identity");
  }

  String content = toYAML(configRoot);
  fs.write(upgradePath, content);
  printf("[LINES] Upgrade config written: %s\n", upgradePath.c_str());
}

void cmdRemove(const String& linesDir, int argc, char** argv) {
  if (argc < 3) {
    printf("Usage: rhod remove <Name>\n");
    return;
  }
  String name = argv[2];
  auto& fs = FilesystemDevice::fs();

  // Try to remove from upgrade, clients, hostAs
  String paths[] = {
    linesDir + "/upgrade/" + name + ".yml",
    linesDir + "/clients/" + name + ".yml",
    linesDir + "/hostAs/" + name + ".yml",
  };
  for (auto& p : paths) {
    fs.unlink(p);
  }
  printf("[LINES] Removed: %s\n", name.c_str());
}

void cmdList(const String& linesDir) {
  auto& fs = FilesystemDevice::fs();
  String portsDir = linesDir + "/ports";
  Stat portsStat = fs.stat(portsDir, 1, 100);

  printf("Bound ports:\n");
  for (usz i = 0; i < portsStat.children.size(); ++i) {
    String name = Resource::Path(portsStat.children[i].path).basename();
    printf("  %s\n", name.c_str());
  }
}

void cmdHooks(const String& linesDir) {
  auto& fs = FilesystemDevice::fs();
  String hookDir = linesDir + "/hook";
  Stat hookStat = fs.stat(hookDir, 1, 100);

  printf("Hooked stations:\n");
  for (usz i = 0; i < hookStat.children.size(); ++i) {
    String name = Resource::Path(hookStat.children[i].path).basename();
    printf("  %s\n", name.c_str());
  }
}

void cmdPid(const String& linesDir) {
  u32 pid = readPidFile(linesDir);
  if (pid > 0 && processExists(pid)) {
    printf("%u\n", pid);
  } else {
    printf("No running daemon\n");
  }
}

void cmdStop(const String& linesDir) {
  u32 pid = readPidFile(linesDir);
  if (pid > 0 && processExists(pid)) {
    kill((pid_t)pid, SIGTERM);
    printf("[LINES] Sent SIGTERM to PID %u\n", pid);
  } else {
    printf("[LINES] No running daemon\n");
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  String linesDir = getLinesDir();

  if (argc < 2) {
    printf("Usage: rhod <command> [args...]\n");
    printf("Commands:\n");
    printf("  start         Start daemon in background\n");
    printf("  daemon        Run daemon in foreground\n");
    printf("  upgrade       Add/update an upgrade config\n");
    printf("  remove        Remove a config\n");
    printf("  list          List bound ports\n");
    printf("  hooks         List hooked stations\n");
    printf("  pid           Show daemon PID\n");
    printf("  stop          Stop the daemon\n");
    return 1;
  }

  String cmd = argv[1];

  if (cmd == "start") {
    cmdStart(linesDir);
  } else if (cmd == "daemon") {
    cmdDaemon(linesDir);
  } else if (cmd == "upgrade") {
    cmdUpgrade(linesDir, argc, argv);
  } else if (cmd == "remove") {
    cmdRemove(linesDir, argc, argv);
  } else if (cmd == "list") {
    cmdList(linesDir);
  } else if (cmd == "hooks") {
    cmdHooks(linesDir);
  } else if (cmd == "pid") {
    cmdPid(linesDir);
  } else if (cmd == "stop") {
    cmdStop(linesDir);
  } else {
    printf("Unknown command: %s\n", cmd.c_str());
    return 1;
  }

  return 0;
}
