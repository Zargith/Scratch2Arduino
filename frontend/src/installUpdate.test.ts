import { afterEach, describe, expect, it, vi } from "vitest";

import { bridge } from "@/bridge";
import { translate, type MessageKey } from "@/i18n";
import { installRelease } from "@/installUpdate";

const release = {
	version: "9.0.0",
	page: "https://github.com/Zargith/Scratch2Arduino/releases/tag/v9.0.0",
	download: "https://github.com/Zargith/Scratch2Arduino/releases/download/v9.0.0/Scratch2Arduino_installer.exe",
};
const t = (key: MessageKey) => translate("fr", key);

describe("update installation", () => {
	afterEach(() => vi.restoreAllMocks());

	it("lets the program download and start the installer", async () => {
		const install = vi.spyOn(bridge, "installUpdate").mockResolvedValue(true);
		const openUrl = vi.spyOn(bridge, "openUrl").mockResolvedValue(true);
		expect(await installRelease(release, "fr", t)).toBe("");
		expect(install).toHaveBeenCalledWith(release.download, "fr");
		expect(openUrl).not.toHaveBeenCalled();
	});

	it("has the browser download the installer when the program cannot", async () => {
		vi.spyOn(bridge, "installUpdate").mockRejectedValue(new Error("Impossible de télécharger"));
		const openUrl = vi.spyOn(bridge, "openUrl").mockResolvedValue(true);
		expect(await installRelease(release, "fr", t)).toBe(t("update.failed_browser"));
		expect(openUrl).toHaveBeenCalledWith(release.download);
	});

	it("shows the error when the browser cannot be opened either", async () => {
		vi.spyOn(bridge, "installUpdate").mockRejectedValue(new Error("Impossible de télécharger"));
		vi.spyOn(bridge, "openUrl").mockResolvedValue(false);
		expect(await installRelease(release, "fr", t)).toBe("Impossible de télécharger");
	});
});
