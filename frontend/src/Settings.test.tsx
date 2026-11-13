import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it, vi } from "vitest";

import { bridge } from "@/bridge";
import { translate, type MessageKey } from "@/i18n";
import { Settings } from "@/Settings";

const release = { version: "9.0.0", page: "https://example.org/notes", download: "https://example.org/setup.exe" };
vi.mock("@/updates", () => ({
	findUpdate: async () => release,
	platform: () => "windows",
}));

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
						board="Arduino Uno"
						settings={{ language: "", checkUpdates: true, theme: "" }}
						onLanguage={onLanguage}
						onSettings={onSettings}
						onUpdate={() => undefined}
						readyToClose={async () => true}
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

		const openUrl = vi.spyOn(bridge, "openUrl").mockResolvedValue(true);
		const report = [...document.querySelectorAll("button")].find((item) => item.textContent === "Signaler un problème");
		await act(async () => report?.click());
		const url = new URL(openUrl.mock.calls[0]?.[0] ?? "");
		expect(url.searchParams.get("body")).toContain("Scratch2Arduino version: 0.9.0");
		expect(url.searchParams.get("body")).toContain("Board: Arduino Uno");
		openUrl.mockRestore();
		await act(async () => root.unmount());
	});

	it("asks before the installer closes the program, and stops when cancelled", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		const installUpdate = vi.spyOn(bridge, "installUpdate").mockResolvedValue(true);
		let allowed = false;
		const readyToClose = vi.fn(async () => allowed);
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
						board="Arduino Uno"
						settings={{ language: "", checkUpdates: true, theme: "" }}
						onLanguage={() => undefined}
						onSettings={() => undefined}
						onUpdate={() => undefined}
						readyToClose={readyToClose}
						onClose={() => undefined}
					/>
				</MantineProvider>
			)
		);
		const button = (label: string) => [...document.querySelectorAll("button")].find((item) => item.textContent === label);
		await act(async () => button("Vérifier maintenant")?.click());
		await act(async () => button("Télécharger et installer")?.click());
		expect(readyToClose).toHaveBeenCalledTimes(1);
		expect(installUpdate).not.toHaveBeenCalled();

		allowed = true;
		await act(async () => button("Télécharger et installer")?.click());
		expect(installUpdate).toHaveBeenCalledWith(release.download, "fr");
		await act(async () => root.unmount());
		installUpdate.mockRestore();
	});
});
