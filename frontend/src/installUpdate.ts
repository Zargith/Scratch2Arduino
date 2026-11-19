// Windows update: the program downloads the installer, starts it and closes. When that fails
// (proxy, antivirus, network that blocks the certificate checks), the browser downloads the
// installer instead: it gets through where the program cannot.
import { bridge } from "@/bridge";
import type { Language } from "@/catalog";
import type { MessageKey } from "@/i18n";
import type { Release } from "@/updates";

/** "" when the installer started (the program closes), else the message to show. */
export async function installRelease(
	release: Release,
	language: Language,
	t: (key: MessageKey) => string
): Promise<string> {
	try {
		if (await bridge.installUpdate(release.download, language)) return "";
		await bridge.openUrl(release.download);
		return "";
	} catch (error) {
		if (await bridge.openUrl(release.download).catch(() => false)) return t("update.failed_browser");
		return error instanceof Error ? error.message : String(error);
	}
}
