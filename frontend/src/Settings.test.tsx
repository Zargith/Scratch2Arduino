import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it, vi } from "vitest";

import { translate, type MessageKey } from "@/i18n";
import { Settings } from "@/Settings";

describe("settings window", () => {
	it("changes the language and the update check at start-up", async () => {
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
		const onLanguage = vi.fn();
		const onSettings = vi.fn();
		const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);
		const container = document.createElement("div");
		document.body.append(container);
		const root = createRoot(container);
		await act(async () =>
			root.render(
				<MantineProvider>
					<Settings
						opened
						language="fr"
						t={t}
						version="0.9.0"
						settings={{ language: "", checkUpdates: true, theme: "" }}
						onLanguage={onLanguage}
						onSettings={onSettings}
						onUpdate={() => undefined}
						onClose={() => undefined}
					/>
				</MantineProvider>
			)
		);
		expect(document.body.textContent).toContain("Version installée : 0.9.0");
		const english = [...document.querySelectorAll("label")].find((label) => label.textContent === "English");
		await act(async () => english?.click());
		expect(onLanguage).toHaveBeenCalledWith("en");

		const checkbox = document.querySelector<HTMLInputElement>('input[type="checkbox"]');
		expect(checkbox?.checked).toBe(true);
		await act(async () => checkbox?.click());
		expect(onSettings).toHaveBeenCalledWith({ language: "", checkUpdates: false, theme: "" });
		await act(async () => root.unmount());
	});
});
