import { MantineProvider } from "@mantine/core";
import { act, type ReactNode } from "react";
import { createRoot, type Root } from "react-dom/client";
import { afterEach, describe, expect, it, vi } from "vitest";

import { BoardPackageDialog, BoardPackageList, formatSize, needsPackage, packageOf } from "@/BoardPackages";
import { bridge, type BoardPackage } from "@/bridge";
import { translate, type MessageKey } from "@/i18n";

const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);

const avr: BoardPackage = {
	id: "arduino-avr",
	name: "Cartes Arduino AVR",
	boards: ["mega", "nano", "uno"],
	shipped: true,
	version: "",
	installedVersion: "1.8.6",
	downloadBytes: 0,
	diskBytes: 260e6,
};
const esp32: BoardPackage = {
	id: "esp32",
	name: "Cartes ESP32",
	boards: ["esp32"],
	shipped: false,
	version: "3.3.12",
	installedVersion: "",
	downloadBytes: 1.94e9,
	diskBytes: 8e9,
};
const esp8266: BoardPackage = {
	id: "esp8266",
	name: "Cartes ESP8266",
	boards: ["esp8266"],
	shipped: false,
	version: "3.1.2",
	installedVersion: "3.1.2",
	downloadBytes: 0,
	diskBytes: 496e6,
};

let root: Root | null = null;

afterEach(async () => {
	vi.restoreAllMocks();
	if (root) await act(async () => root?.unmount());
	root = null;
	document.body.innerHTML = "";
});

async function render(element: ReactNode) {
	const container = document.createElement("div");
	document.body.append(container);
	root = createRoot(container);
	await act(async () => root?.render(<MantineProvider>{element}</MantineProvider>));
}

const button = (label: string, within: ParentNode = document) =>
	[...within.querySelectorAll("button")].find((item) => item.textContent === label);

describe("board packages", () => {
	it("writes sizes like the core", () => {
		expect(formatSize(1.94e9, "fr")).toBe("1,9 Go");
		expect(formatSize(1.94e9, "en")).toBe("1.9 GB");
		expect(formatSize(8e9, "en")).toBe("8 GB");
		expect(formatSize(12.4e9, "en")).toBe("12 GB");
		expect(formatSize(125e6, "fr")).toBe("125 Mo");
		expect(formatSize(1.5e6, "en")).toBe("1.5 MB");
		expect(formatSize(1200, "fr")).toBe("2 ko");
	});

	it("tells which boards need a package", () => {
		const packages = [avr, esp32, esp8266];
		expect(packageOf(packages, "esp32")?.id).toBe("esp32");
		expect(needsPackage(packages, "esp32")).toBe(true);
		expect(needsPackage(packages, "uno")).toBe(false); // shipped
		expect(needsPackage(packages, "esp8266")).toBe(false); // installed
		expect(needsPackage(null, "esp32")).toBe(false); // list not read yet: never block
	});

	it("lists the packages with their sizes and installs one", async () => {
		const install = vi.spyOn(bridge, "boardPackageInstall").mockResolvedValue({ ok: true, message: "", details: "" });
		const onChanged = vi.fn(async () => undefined);
		await render(<BoardPackageList language="fr" t={t} packages={[avr, esp32, esp8266]} onChanged={onChanged} />);
		const text = document.body.textContent ?? "";
		expect(text).toContain("Fournies avec Scratch2Arduino");
		expect(text).toContain("Environ 1,9 Go à télécharger, 8 Go sur le disque");
		expect(text).toContain("Version 3.1.2, 496 Mo sur le disque");
		// The shipped package has no button.
		expect(document.querySelector('[data-testid="package-arduino-avr"] button')).toBeNull();

		const card = document.querySelector('[data-testid="package-esp32"]') as HTMLElement;
		await act(async () => button("Installer", card)?.click());
		expect(install).toHaveBeenCalledWith("esp32", "fr");
		expect(onChanged).toHaveBeenCalled();
		expect(document.body.textContent).toContain("Cartes ESP32 : installées.");
	});

	it("asks before removing a package", async () => {
		const remove = vi.spyOn(bridge, "boardPackageRemove").mockResolvedValue({ ok: true, message: "", details: "" });
		const confirm = vi.spyOn(window, "confirm").mockReturnValue(false);
		await render(<BoardPackageList language="fr" t={t} packages={[avr, esp32, esp8266]} onChanged={async () => undefined} />);
		const card = document.querySelector('[data-testid="package-esp8266"]') as HTMLElement;
		await act(async () => button("Supprimer", card)?.click());
		expect(confirm).toHaveBeenCalledWith(expect.stringContaining("496 Mo"));
		expect(remove).not.toHaveBeenCalled();

		confirm.mockReturnValue(true);
		await act(async () => button("Supprimer", card)?.click());
		expect(remove).toHaveBeenCalledWith("esp8266", "fr");
	});

	it("offers the package of a chosen board, and goes on once installed", async () => {
		const install = vi.spyOn(bridge, "boardPackageInstall").mockResolvedValue({ ok: true, message: "", details: "" });
		const onInstalled = vi.fn(async () => undefined);
		await render(
			<BoardPackageDialog language="fr" t={t} boardId="esp32" packages={[avr, esp32]} onInstalled={onInstalled} onClose={() => undefined} />
		);
		expect(document.body.textContent).toContain("environ 1,9 Go à télécharger et 8 Go sur le disque");
		await act(async () => button("Installer")?.click());
		expect(install).toHaveBeenCalledWith("esp32", "fr");
		expect(onInstalled).toHaveBeenCalled();
	});

	it("shows why an install failed and keeps the dialog open", async () => {
		vi.spyOn(bridge, "boardPackageInstall").mockResolvedValue({
			ok: false,
			message: "Pas de connexion internet.",
			details: "dial tcp: no such host",
		});
		const onInstalled = vi.fn(async () => undefined);
		const onClose = vi.fn();
		await render(
			<BoardPackageDialog language="fr" t={t} boardId="esp32" packages={[avr, esp32]} onInstalled={onInstalled} onClose={onClose} />
		);
		await act(async () => button("Installer")?.click());
		expect(onInstalled).not.toHaveBeenCalled();
		expect(document.body.textContent).toContain("Pas de connexion internet.");
		await act(async () => button("Plus tard")?.click());
		expect(onClose).toHaveBeenCalled();
	});

	it("stays closed for a board that needs nothing", async () => {
		await render(
			<BoardPackageDialog language="fr" t={t} boardId="uno" packages={[avr]} onInstalled={async () => undefined} onClose={() => undefined} />
		);
		expect(button("Installer")).toBeUndefined();
	});
});
