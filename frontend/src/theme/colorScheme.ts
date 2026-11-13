// Light / dark appearance chosen in the Settings ("": the one of the computer).
import { useMantineColorScheme, type MantineColorScheme } from "@mantine/core";
import { useEffect, useRef } from "react";

/**
 * Applies the saved appearance, only when it changes. Mantine's setColorScheme is a new function
 * at each render and, each time it is called, switches off every transition of the page for a
 * moment and writes the scheme again: called at every render of the editor (Live polling,
 * typing...), it made the window flicker.
 */
export function useSavedColorScheme(theme: "" | "light" | "dark"): void {
	const { colorScheme, setColorScheme } = useMantineColorScheme();
	const wanted: MantineColorScheme = theme === "" ? "auto" : theme;
	const set = useRef(setColorScheme);
	set.current = setColorScheme;
	useEffect(() => {
		if (colorScheme !== wanted) set.current(wanted);
	}, [colorScheme, wanted]);
}
