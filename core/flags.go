package core

import (
	"flag"
	"os"
)

type appFlags struct {
	root    string
	tcp     string
	noUI    bool
	noTray  bool
	noAdmin bool
}

func newFlagSet(args []string) appFlags {
	fs := flag.NewFlagSet("liveaio", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	root := fs.String("root", "", "LIVEAIO app root")
	tcp := fs.String("tcp", DefaultTCPAddr, "JSONL TCP")
	noUI := fs.Bool("no-ui", false, "do not open UI")
	noTray := fs.Bool("no-tray", false, "no tray; keep running in foreground")
	noAdmin := fs.Bool("no-admin", false, "skip UAC")

	argv := args
	if len(argv) > 0 {
		argv = argv[1:]
	}
	// Host may pass module tokens before flags.
	filtered := make([]string, 0, len(argv))
	for _, a := range argv {
		switch a {
		case "tools", "--tools", "pages", "--pages":
			continue
		default:
			filtered = append(filtered, a)
		}
	}
	_ = fs.Parse(filtered)
	return appFlags{
		root:    *root,
		tcp:     *tcp,
		noUI:    *noUI,
		noTray:  *noTray,
		noAdmin: *noAdmin,
	}
}
