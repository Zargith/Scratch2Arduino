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
						boardPackages={null}
						onBoardPackagesChanged={async () => undefined}
						hasHost={false}
						onLanguage={onLanguage}
						onSettings={onSettings}
						update={null}
						onUpdate={() => undefined}
						installing={false}
						installError=""
						onInstall={() => undefined}
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

	it("installs the release found by Check now", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		const onInstall = vi.fn();
		const onUpdate = vi.fn();
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
						boardPackages={null}
						onBoardPackagesChanged={async () => undefined}
						hasHost={false}
						onLanguage={() => undefined}
						onSettings={() => undefined}
						update={null}
						onUpdate={onUpdate}
						installing={false}
						installError=""
						onInstall={onInstall}
						onClose={() => undefined}
					/>
				</MantineProvider>
			)
		);
		const button = (label: string) => [...document.querySelectorAll("button")].find((item) => item.textContent === label);
		expect(button("Télécharger et installer")).toBeUndefined();
		await act(async () => button("Vérifier maintenant")?.click());
		expect(onUpdate).toHaveBeenCalledWith(release);
		await act(async () => button("Télécharger et installer")?.click());
		expect(onInstall).toHaveBeenCalledWith(release);
		await act(async () => root.unmount());
	});

	it("shows the release already found without checking again", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
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
						boardPackages={null}
						onBoardPackagesChanged={async () => undefined}
						hasHost={false}
						onLanguage={() => undefined}
						onSettings={() => undefined}
						update={release}
						onUpdate={() => undefined}
						installing
						installError="Le téléchargement a échoué"
						onInstall={() => undefined}
						onClose={() => undefined}
					/>
				</MantineProvider>
			)
		);
		expect(document.body.textContent).toContain(t("update.available", { version: "9.0.0", current: "0.9.0" }));
		expect(document.body.textContent).toContain(t("settings.downloading"));
		expect(document.body.textContent).toContain("Le téléchargement a échoué");
		const install = [...document.querySelectorAll("button")].find((item) => item.textContent?.includes("Télécharger et installer"));
		expect(install?.disabled).toBe(true); // loading: not clickable twice
		await act(async () => root.unmount());
	});
});
