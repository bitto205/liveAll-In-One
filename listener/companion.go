package listener

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"syscall"
	"time"

	"golang.org/x/sys/windows/registry"
)

const (
	proxyValue   = "127.0.0.1:19088,direct://"
	shellProcess = "proxy_shell.exe"
	companionKey = "companion_install_dir"
)

var (
	proxySwitchRE = regexp.MustCompile(`(\.commandLine\.appendSwitch\s*\(\s*["']proxy-server["'],\s*["'])([^"']*?)(["'])`)
	spawnTraceRE  = regexp.MustCompile(`;\(function\(\)\{var c=require\("child_process"\);try\{c\.spawn\("[^"]+",\[\],\{detached:false,stdio:"ignore",windowsHide:true\}\);\}catch\(e\)\{\}\}\)\(\);`)
	proxyInjectRE = regexp.MustCompile(`\w+\.commandLine\.appendSwitch\("proxy-server","127\.0\.0\.1:19088,direct://"\);`)
)

func aioDataDir() string {
	home, _ := os.UserHomeDir()
	aio := filepath.Join(home, ".liveaio")
	if st, err := os.Stat(aio); err == nil && st.IsDir() {
		return aio
	}
	legacy := filepath.Join(home, ".livehelper")
	if st, err := os.Stat(legacy); err == nil && st.IsDir() {
		return legacy
	}
	_ = os.MkdirAll(aio, 0755)
	return aio
}

func buildSpawnCode(dest string) string {
	jsPath := strings.ReplaceAll(dest, `\`, `\\`)
	return fmt.Sprintf(
		`;(function(){var c=require("child_process");try{c.spawn("%s",[],{detached:false,stdio:"ignore",windowsHide:true});}catch(e){}})();`,
		jsPath,
	)
}

func readConfigMap(root string) map[string]any {
	raw, err := os.ReadFile(filepath.Join(root, "config.json"))
	if err != nil {
		return map[string]any{}
	}
	var m map[string]any
	if json.Unmarshal(raw, &m) != nil {
		return map[string]any{}
	}
	return m
}

func writeConfigKey(root, key string, val any) error {
	path := filepath.Join(root, "config.json")
	m := readConfigMap(root)
	m[key] = val
	b, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, b, 0644)
}

func scanCompanionInstallDir() string {
	hives := []registry.Key{registry.LOCAL_MACHINE, registry.CURRENT_USER}
	subkeys := []string{
		`SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall`,
		`SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall`,
	}
	for _, hive := range hives {
		for _, sub := range subkeys {
			k, err := registry.OpenKey(hive, sub, registry.ENUMERATE_SUB_KEYS|registry.QUERY_VALUE)
			if err != nil {
				continue
			}
			names, _ := k.ReadSubKeyNames(-1)
			for _, name := range names {
				sk, err := registry.OpenKey(k, name, registry.QUERY_VALUE)
				if err != nil {
					continue
				}
				dn, _, _ := sk.GetStringValue("DisplayName")
				loc, _, _ := sk.GetStringValue("InstallLocation")
				sk.Close()
				if strings.Contains(dn, "\u76f4\u64ad\u4f34\u4fa3") && loc != "" {
					if st, err := os.Stat(loc); err == nil && st.IsDir() {
						k.Close()
						return loc
					}
				}
			}
			k.Close()
		}
	}
	return ""
}

func GetManualCompanionDir(root string) string {
	m := readConfigMap(root)
	s, _ := m[companionKey].(string)
	return strings.TrimSpace(s)
}

func SetManualCompanionDir(root, pathStr string) (bool, string) {
	pathStr = strings.TrimSpace(pathStr)
	if pathStr == "" {
		return false, "empty path"
	}
	if st, err := os.Stat(pathStr); err != nil || !st.IsDir() {
		return false, "path is not a directory"
	}
	if err := writeConfigKey(root, companionKey, pathStr); err != nil {
		return false, err.Error()
	}
	return true, pathStr
}

func GetCompanionInstallDir(root string) string {
	if m := GetManualCompanionDir(root); m != "" {
		if st, err := os.Stat(m); err == nil && st.IsDir() {
			return m
		}
	}
	return scanCompanionInstallDir()
}

func indexJSCandidates(root string) []string {
	var out []string
	cfgPath := filepath.Join(root, "launcher_config.json")
	if raw, err := os.ReadFile(cfgPath); err == nil {
		var cfg map[string]any
		if json.Unmarshal(raw, &cfg) == nil {
			for _, key := range []string{"cur_path", "new_path"} {
				if ver, _ := cfg[key].(string); ver != "" {
					out = append(out,
						filepath.Join(root, ver, "resources", "app", "index.js"),
						filepath.Join(root, ver, "resources", "app", "app.asar.unpacked", "index.js"),
					)
				}
			}
		}
	}
	out = append(out,
		filepath.Join(root, "resources", "app", "index.js"),
		filepath.Join(root, "resources", "app", "app.asar.unpacked", "index.js"),
	)
	return out
}

func FindIndexJS(root string) string {
	dir := GetCompanionInstallDir(root)
	if dir == "" {
		return ""
	}
	for _, p := range indexJSCandidates(dir) {
		if st, err := os.Stat(p); err == nil && !st.IsDir() {
			return p
		}
	}
	return ""
}

func applyPatch(content, dest, indexPath string) (string, catalog, string) {
	cat := catalog{IndexPath: filepath.Clean(indexPath), Spawn: buildSpawnCode(dest)}
	newContent := content
	if proxySwitchRE.FindStringSubmatchIndex(content) != nil {
		cat.ProxyMode = "replace"
		replaced := false
		newContent = proxySwitchRE.ReplaceAllStringFunc(content, func(s string) string {
			if replaced {
				return s
			}
			replaced = true
			sm := proxySwitchRE.FindStringSubmatch(s)
			if len(sm) == 4 {
				return sm[1] + proxyValue + sm[3]
			}
			return s
		})
	} else {
		readyRE := regexp.MustCompile(`(\b(\w+)\.on\s*\(\s*['"]ready['"])`)
		rm := readyRE.FindStringSubmatchIndex(newContent)
		if rm == nil {
			return "", catalog{}, "No suitable injection point found in index.js"
		}
		appVar := readyRE.FindStringSubmatch(newContent)[2]
		proxyInject := fmt.Sprintf(`%s.commandLine.appendSwitch("proxy-server","%s");`, appVar, proxyValue)
		cat.ProxyMode = "inject"
		cat.ProxyInject = proxyInject
		newContent = newContent[:rm[0]] + proxyInject + newContent[rm[0]:]
	}
	spawn := cat.Spawn
	idx := strings.Index(newContent, "proxy-server")
	if idx >= 0 {
		lineStart := strings.LastIndex(newContent[:idx], ";") + 1
		newContent = newContent[:lineStart] + spawn + newContent[lineStart:]
	}
	okRE := regexp.MustCompile(`,!\w+\.ok\)`)
	newContent = okRE.ReplaceAllString(newContent, ",false)")
	return newContent, cat, ""
}

// PatchCompanion patches companion index.js and deploys proxy_shell.exe.
func PatchCompanion(root string) (bool, string) {
	path := FindIndexJS(root)
	if path == "" {
		return false, "Companion install directory not found"
	}
	src := bundledShell(root)
	if _, err := os.Stat(src); err != nil {
		return false, "Bundled proxy_shell.exe not found in listener directory"
	}
	dest := filepath.Join(filepath.Dir(path), shellProcess)
	if IsCompanionPatched(root) {
		_ = InstallCACert()
		return true, "Already patched"
	}
	original, err := os.ReadFile(path)
	if err != nil {
		return false, fmt.Sprintf("read index.js: %v", err)
	}
	text := stripPatchTraces(string(original))
	newContent, cat, errMsg := applyPatch(text, dest, path)
	if newContent == "" {
		return false, errMsg
	}
	if err := copyFile(src, dest); err != nil {
		return false, fmt.Sprintf("copy proxy_shell.exe: %v", err)
	}
	if err := os.WriteFile(path, []byte(newContent), 0644); err != nil {
		return false, fmt.Sprintf("write index.js: %v", err)
	}
	b, _ := json.MarshalIndent(cat, "", "  ")
	_ = os.WriteFile(catalogPath(), b, 0644)
	_ = InstallCACert()
	_ = BootstrapProxyShellCA(dest)
	return true, "Patch successful. Restart companion app to take effect."
}

func UnpatchCompanion(root string) (bool, string) {
	path := FindIndexJS(root)
	if path == "" {
		return false, "Companion install not found"
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return false, fmt.Sprintf("read index.js: %v", err)
	}
	restored := stripPatchTraces(string(raw))
	if contentHasPatchTraces(restored) {
		return false, "patch traces remain"
	}
	if err := os.WriteFile(path, []byte(restored), 0644); err != nil {
		return false, fmt.Sprintf("write index.js: %v", err)
	}
	_ = os.Remove(catalogPath())
	return true, "unpatched"
}

func UnpatchLocal(root string) (bool, string) {
	// Route3 no longer rewrites index.js permanently; clear leftover traces if any.
	return UnpatchCompanion(root)
}

func stripPatchTraces(content string) string {
	out := spawnTraceRE.ReplaceAllString(content, "")
	out = proxyInjectRE.ReplaceAllString(out, "")
	if strings.Contains(out, proxyValue) {
		out = proxySwitchRE.ReplaceAllStringFunc(out, func(s string) string {
			sm := proxySwitchRE.FindStringSubmatch(s)
			if len(sm) == 4 && sm[2] == proxyValue {
				return sm[1] + "direct://" + sm[3]
			}
			return s
		})
	}
	return out
}

func contentHasPatchTraces(content string) bool {
	if strings.Contains(content, "127.0.0.1:19088") || strings.Contains(content, shellProcess) {
		return true
	}
	return spawnTraceRE.MatchString(content)
}

func IsCompanionPatched(root string) bool {
	raw, err := os.ReadFile(catalogPath())
	if err != nil {
		return false
	}
	var cat catalog
	if json.Unmarshal(raw, &cat) != nil || cat.IndexPath == "" {
		return false
	}
	text, err := os.ReadFile(cat.IndexPath)
	if err != nil {
		return false
	}
	body := string(text)
	if cat.Spawn == "" || !strings.Contains(body, cat.Spawn) {
		return false
	}
	deployed := filepath.Join(filepath.Dir(cat.IndexPath), shellName)
	src := bundledShell(root)
	if _, err := os.Stat(deployed); err != nil {
		return false
	}
	if _, err := os.Stat(src); err == nil {
		a, _ := os.ReadFile(src)
		b, _ := os.ReadFile(deployed)
		if len(a) > 0 && string(a) != string(b) {
			return false
		}
	}
	return true
}

func copyFile(src, dst string) error {
	in, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	return os.WriteFile(dst, in, 0644)
}

func caPaths() (cert, key string) {
	dir := aioDataDir()
	return filepath.Join(dir, "proxy_shell_ca.crt"), filepath.Join(dir, "proxy_shell_ca.key")
}

func InstallCACert() error {
	cert, _ := caPaths()
	if _, err := os.Stat(cert); err != nil {
		return err
	}
	cmd := exec.Command("certutil", "-addstore", "-f", "ROOT", cert)
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
	_, _ = cmd.CombinedOutput()
	return nil
}

func BootstrapProxyShellCA(shellExe string) error {
	if shellExe == "" {
		return nil
	}
	cmd := exec.Command(shellExe)
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
	if err := cmd.Start(); err != nil {
		return InstallCACert()
	}
	time.Sleep(2 * time.Second)
	if cmd.Process != nil {
		_ = cmd.Process.Kill()
	}
	return InstallCACert()
}

func PageCheckRoute4(root string) map[string]any {
	install := GetCompanionInstallDir(root)
	index := FindIndexJS(root)
	reg := scanCompanionInstallDir() != ""
	patched := IsCompanionPatched(root)
	out := map[string]any{
		"route":                  "4",
		"ok":                     true,
		"ready":                  patched,
		"companion_in_registry":  reg,
		"companion_installed":    install != "",
		"manual_path_invalid":    GetManualCompanionDir(root) != "" && install == "",
		"manual_companion_dir":   install,
		"index_js_found":         index != "",
		"is_patched":             patched,
		"index_patched":          patched,
		"index_modified":         index != "" && contentHasPatchTraces(mustRead(index)),
		"exe_identical":          patched,
		"exe_in_place":           false,
		"patch_needed":           index != "" && !patched,
		"message":                "",
	}
	if index != "" {
		dest := filepath.Join(filepath.Dir(index), shellProcess)
		if st, err := os.Stat(dest); err == nil && !st.IsDir() {
			out["exe_in_place"] = true
		}
	}
	_ = InstallCACert()
	return out
}

func PageCheckRoute3(root string) map[string]any {
	proxy, _ := getSystemProxy()
	fields := PageCheckRoute4(root)
	fields["route"] = "3"
	fields["system_proxy"] = proxy.Enable
	fields["proxy_server"] = proxy.Server
	fields["ready"] = fields["companion_installed"] == true
	fields["is_patched"] = false
	fields["patch_needed"] = false
	return fields
}

type proxySnapshot struct {
	Enable bool
	Server string
}

func getSystemProxy() (proxySnapshot, error) {
	k, err := registry.OpenKey(registry.CURRENT_USER,
		`Software\Microsoft\Windows\CurrentVersion\Internet Settings`, registry.QUERY_VALUE)
	if err != nil {
		return proxySnapshot{}, err
	}
	defer k.Close()
	enable, _, _ := k.GetIntegerValue("ProxyEnable")
	server, _, _ := k.GetStringValue("ProxyServer")
	return proxySnapshot{Enable: enable != 0, Server: server}, nil
}

func setSystemProxy(server string, enable bool) error {
	k, err := registry.OpenKey(registry.CURRENT_USER,
		`Software\Microsoft\Windows\CurrentVersion\Internet Settings`, registry.SET_VALUE)
	if err != nil {
		return err
	}
	defer k.Close()
	var en uint32
	if enable {
		en = 1
	}
	if err := k.SetDWordValue("ProxyEnable", en); err != nil {
		return err
	}
	if server != "" {
		if err := k.SetStringValue("ProxyServer", server); err != nil {
			return err
		}
	}
	return nil
}

func restoreSystemProxy(prev proxySnapshot) error {
	if err := setSystemProxy(prev.Server, prev.Enable); err != nil {
		return fmt.Errorf("restore proxy: %w", err)
	}
	return nil
}

func hideCmd(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: 0x08000000}
}

func mustRead(path string) string {
	b, err := os.ReadFile(path)
	if err != nil {
		return ""
	}
	return string(b)
}
