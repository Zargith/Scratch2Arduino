import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { afterEach, describe, expect, it, vi } from "vitest";

import { addLibrary, ArduinoLibraries, includeLines } from "@/ArduinoLibraries";
import type { InstalledLibrary, LibraryChange, RegistryLibrary } from "@/bridge";
import { translate, type MessageKey } from "@/i18n";

const servo: InstalledLibrary = {
	name: "Servo",
	version: "1.3.0",
	author: "Arduino",
	sentence: "Servos.",
	website: "",
	includes: ["Servo.h", "Servo.hpp"],
	builtIn: false,
};

describe("library manager helpers", () => {
	it("adds or replaces a library, sorted by name", () => {
		expect(addLibrary([{ name: "Servo" }], { name: "ArduinoJson" })).toEqual([{ name: "ArduinoJson" }, { name: "Servo" }]);
		expect(addLibrary([{ name: "Servo" }], { name: "Servo", version: "1.2.0" })).toEqual([{ name: "Servo", version: "1.2.0" }]);
	});

	it("tells which lines to write at the top of the code", () => {
		expect(includeLines(servo)).toBe("#include <Servo.h>");
		expect(includeLines(undefined)).toBe("");
	});
});

describe("library manager window", () => {
	afterEach(() => {
		for (const name of ["s2a_hello", "s2a_libraries_installed", "s2a_libraries_search", "s2a_library_install"] as const) delete window[name];
		document.body.innerHTML = "";
	});

	it("finds a library and adds it to the project once installed", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		window.matchMedia ??= (() => ({
			matches: false,
			addEventListener() {},
			removeEventListener() {},
			addListener() {},
			removeListener() {},
		})) as never;
		globalThis.ResizeObserver ??= class {
			observe() {}
			unobserve() {}
			disconnect() {}
		} as never;
		const found: RegistryLibrary = {
			name: "ArduinoJson",
			author: "Benoit",
			sentence: "JSON.",
			category: "",
			website: "",
			latestVersion: "7.2.0",
			versions: ["7.1.0", "7.2.0"],
			architectures: ["*"],
		};
		window.s2a_hello = vi.fn();
		window.s2a_libraries_installed = vi.fn(async () => [servo]);
		window.s2a_libraries_search = vi.fn(async () => [found]);
		const install = vi.fn(async (): Promise<LibraryChange> => ({ ok: true, message: "", details: "" }));
		window.s2a_library_install = install;
		const onChange = vi.fn();
		const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);
		const container = document.createElement("div");
		document.body.append(container);
		const root = createRoot(container);
		await act(async () =>
			root.render(
				<MantineProvider>
					<ArduinoLibraries opened language="fr" t={t} libraries={[]} usesCpp onChange={onChange} onClose={() => undefined} />
				</MantineProvider>
			)
		);
		const input = document.querySelector<HTMLInputElement>(`input[aria-label="${t("libraries.search")}"]`);
		expect(input).not.toBeNull();
		await act(async () => {
			const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value")?.set;
			setter?.call(input, "json");
			input?.dispatchEvent(new Event("input", { bubbles: true }));
		});
		await act(async () => input?.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", bubbles: true })));
		expect(window.s2a_libraries_search).toHaveBeenCalledWith("json", "fr");
		expect(document.body.textContent).toContain("ArduinoJson");
		const add = [...document.querySelectorAll("button")].find((button) => button.textContent === t("libraries.add"));
		await act(async () => add?.click());
		expect(install).toHaveBeenCalledWith("ArduinoJson", "", "fr");
		expect(onChange).toHaveBeenCalledWith([{ name: "ArduinoJson" }]);
		expect(document.body.textContent).toContain(t("libraries.added", { name: "ArduinoJson" }));
		await act(async () => root.unmount());
	});
});
