package core

type Features struct {
	Themes            []string `json:"themes"`
	SupportsTray      bool     `json:"supports_tray"`
	SupportsDetachUI  bool     `json:"supports_detach_ui"`
	SupportsShutdown  bool     `json:"supports_shutdown"`
	SupportsToolsHost bool     `json:"supports_tools_host"`
}

func DefaultFeatures() Features {
	return Features{
		Themes:            []string{"拿铁奶咖", "深焙摩卡", "极夜深蓝", "日晷护眼"},
		SupportsTray:      true,
		SupportsDetachUI:  true,
		SupportsShutdown:  true,
		SupportsToolsHost: true,
	}
}
