package main

/*
#include <stdlib.h>
*/
import "C"

import (
	"os"
	"unsafe"

	"liveaio/core"
)

//export LiveAIO_CoreMain
func LiveAIO_CoreMain(argc C.int, argv **C.char) C.int {
	n := int(argc)
	args := make([]string, 0, n)
	if n > 0 && argv != nil {
		hdr := struct {
			Data unsafe.Pointer
			Len  int
			Cap  int
		}{Data: unsafe.Pointer(argv), Len: n, Cap: n}
		cargs := *(*[]*C.char)(unsafe.Pointer(&hdr))
		for _, p := range cargs {
			if p == nil {
				args = append(args, "")
				continue
			}
			args = append(args, C.GoString(p))
		}
	}
	if len(args) == 0 {
		args = []string{os.Args[0]}
	}
	return C.int(core.Run(args))
}

func main() {}
