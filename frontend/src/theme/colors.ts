// Copied from @epitech/tools-react-components (src/components/layout/EpitechColors.ts).
// Keep in sync by hand: the package lives on a private registry.
import type { MantineColorsTuple } from "@mantine/core";

export const EpitechBrandHues = {
	"blue-light": "#013afb",
	"blue-secondary": "#809dfd",
	"orange": "#ff5f3a",
	"orange-secondary": "#ffb29f",
	"green": "#00c475",
	"green-bright": "#00ff97",
	"green-secondary": "#a6ffcc",
	"pink": "#ff1ef7",
	"pink-secondary": "#ff98fc",
} as const;

export type EpitechBrandHue = keyof typeof EpitechBrandHues;

// prettier-ignore
export const EpitechColorRamps: Record<EpitechBrandHue, MantineColorsTuple> = {
	"blue-light":       ["#eef4ff", "#cddeff", "#a3c2ff", "#79a4ff", "#5388ff", "#2a66ff", "#013afb", "#0030df", "#0028c2", "#0020a5"],
	"blue-secondary":   ["#eff3ff", "#dfe7ff", "#cad8ff", "#b6c9ff", "#a4bbff", "#92acff", "#809dfd", "#7590e9", "#6982d6", "#5e75c2"],
	"orange":           ["#fff0ec", "#ffded6", "#ffc7ba", "#ffb09c", "#ff9980", "#ff7e60", "#ff5f3a", "#ea5532", "#d64b2b", "#c24224"],
	"orange-secondary": ["#fff0ec", "#ffe7e1", "#ffdcd4", "#ffd1c6", "#ffc8ba", "#ffbdac", "#ffb29f", "#eda593", "#dc9887", "#cb8b7b"],
	"green":            ["#e3fbea", "#caf4d9", "#acebc3", "#8de1ae", "#70d89b", "#4bce88", "#00c475", "#00b46b", "#00a461", "#009457"],
	"green-bright":     ["#d7ffe4", "#c8ffda", "#b2ffcd", "#9affc0", "#7fffb3", "#5affa5", "#00ff97", "#00ee8c", "#00dd82", "#00cc78"],
	"green-secondary":  ["#d6ffe6", "#d0ffe2", "#c8ffde", "#bfffd9", "#b7ffd5", "#aeffd0", "#a6ffcc", "#9aefbf", "#8fdfb1", "#84d0a4"],
	"pink":             ["#ffedfd", "#ffdafb", "#ffbff8", "#ffa3f7", "#ff86f6", "#ff61f6", "#ff1ef7", "#eb13e3", "#d706d0", "#c200bc"],
	"pink-secondary":   ["#ffedfe", "#ffe2fd", "#ffd4fd", "#ffc5fc", "#ffb7fc", "#ffa8fc", "#ff98fc", "#ed8cea", "#dc80d9", "#ca75c8"],
};

export const EpitechPrimaryShade = { light: 6, dark: 6 } as const;
