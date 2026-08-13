//go:build windows

package launcher

import (
	"fmt"
	"os"
	"strings"
	"unsafe"

	"golang.org/x/sys/windows"
)

// EnsureAdmin re-launches the current executable with UAC elevation if needed.
// Parent process exits after scheduling elevation.
func EnsureAdmin(exePath, args, workDir string) error {
	if isAdmin() {
		return nil
	}
	verb, _ := windows.UTF16PtrFromString("runas")
	file, _ := windows.UTF16PtrFromString(exePath)
	param, _ := windows.UTF16PtrFromString(args)
	cwd, _ := windows.UTF16PtrFromString(workDir)

	shell32 := windows.NewLazySystemDLL("shell32.dll")
	shellExecute := shell32.NewProc("ShellExecuteW")
	show := uintptr(1)
	ret, _, _ := shellExecute.Call(
		0,
		uintptr(unsafe.Pointer(verb)),
		uintptr(unsafe.Pointer(file)),
		uintptr(unsafe.Pointer(param)),
		uintptr(unsafe.Pointer(cwd)),
		show,
	)
	if ret <= 32 {
		return fmt.Errorf("UAC elevation failed (code %d)", ret)
	}
	os.Exit(0)
	return nil
}

func isAdmin() bool {
	var token windows.Token
	proc, err := windows.GetCurrentProcess()
	if err != nil {
		return false
	}
	if err := windows.OpenProcessToken(proc, windows.TOKEN_QUERY, &token); err != nil {
		return false
	}
	defer token.Close()

	var elevation struct {
		TokenIsElevated uint32
	}
	var outLen uint32
	err = windows.GetTokenInformation(
		token,
		windows.TokenElevation,
		(*byte)(unsafe.Pointer(&elevation)),
		uint32(unsafe.Sizeof(elevation)),
		&outLen,
	)
	if err != nil {
		return false
	}
	return elevation.TokenIsElevated != 0
}

// QuoteArg wraps an argument for ShellExecute parameter string.
func QuoteArg(s string) string {
	if s == "" {
		return `""`
	}
	needs := false
	for _, r := range s {
		if r == ' ' || r == '\t' || r == '"' {
			needs = true
			break
		}
	}
	if !needs {
		return s
	}
	out := `"`
	for _, r := range s {
		if r == '"' {
			out += `\`
		}
		out += string(r)
	}
	out += `"`
	return out
}

func BuildElevatedParams(argv []string) string {
	if len(argv) <= 1 {
		return ""
	}
	var parts []string
	for _, a := range argv[1:] {
		parts = append(parts, QuoteArg(a))
	}
	return strings.Join(parts, " ")
}
