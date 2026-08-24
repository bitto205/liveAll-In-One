package resources

import (
	"os"
	"path/filepath"
)

// SkinDir is resources/skin/<tool>/<skinId>.
func SkinDir(root, tool, skinID string) string {
	if root == "" {
		root = "."
	}
	if skinID == "" {
		skinID = "default"
	}
	return filepath.Join(root, "resources", "skin", tool, skinID)
}

// SkinJSONPath is resources/skin/<tool>/<skinId>/skin.json.
func SkinJSONPath(root, tool, skinID string) string {
	return filepath.Join(SkinDir(root, tool, skinID), "skin.json")
}

// SkinExists reports whether skin.json is present.
func SkinExists(root, tool, skinID string) bool {
	st, err := os.Stat(SkinJSONPath(root, tool, skinID))
	return err == nil && !st.IsDir()
}
