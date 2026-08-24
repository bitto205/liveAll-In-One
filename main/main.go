package main

import (
	"os"

	"liveaio/core"
)

// Debug / go run · go build ./main — embeds Go in one exe (no Core dll).
func main() {
	os.Exit(core.Run(os.Args))
}
