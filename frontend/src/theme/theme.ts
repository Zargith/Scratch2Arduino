// Trimmed copy of the Epitech Mantine theme from @epitech/tools-react-components
// (src/components/layout/EpitechMantineTheme.ts). Differences: no Google Fonts (the app
// runs offline, fonts come from @fontsource), and only the parts this app uses.
import "@fontsource/ibm-plex-sans/400.css";
import "@fontsource/ibm-plex-sans/500.css";
import "@fontsource/ibm-plex-sans/700.css";
import "@fontsource/space-mono/400.css";
import "@fontsource/space-mono/700.css";

import { createTheme, v8CssVariablesResolver, virtualColor, type CSSVariablesResolver } from "@mantine/core";

import { EpitechColorRamps, EpitechPrimaryShade } from "@/theme/colors";

const schemeVariables = {
	dark: {
		"--epitech-page": "#181818",
		"--epitech-surface": "#1f1f1f",
		"--epitech-border": "rgba(255,255,255,.10)",
		"--epitech-muted": "rgba(255,255,255,.55)",
	},
	light: {
		"--epitech-page": "#f4f4f2",
		"--epitech-surface": "#ffffff",
		"--epitech-border": "rgba(24,24,24,.14)",
		"--epitech-muted": "rgba(24,24,24,.65)",
	},
} as const;

export const theme = createTheme({
	colors: {
		...EpitechColorRamps,
		blue: virtualColor({ name: "blue", light: "blue-light", dark: "blue-secondary" }),
	},
	primaryColor: "blue",
	primaryShade: EpitechPrimaryShade,
	defaultRadius: "xs",
	fontFamily: "IBM Plex Sans, sans-serif",
	fontFamilyMonospace: "Space Mono, monospace",
	autoContrast: true,
});

export const cssVariablesResolver: CSSVariablesResolver = (resolvedTheme) => {
	const resolved = v8CssVariablesResolver(resolvedTheme);
	Object.assign(resolved.light, schemeVariables.light);
	Object.assign(resolved.dark, schemeVariables.dark);
	return resolved;
};
