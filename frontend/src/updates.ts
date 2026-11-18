// Looks for a newer release of Scratch2Arduino on GitHub. The 0.x releases are pre-releases,
// so the list of releases is read (not "latest", which skips them). Never throws on a missing
// network: the caller just shows that the check could not be done.

export const repositoryUrl = "https://github.com/Zargith/Scratch2Arduino";
const releasesApi = "https://api.github.com/repos/Zargith/Scratch2Arduino/releases?per_page=20";

export interface Release {
	/** "0.9.0" (the tag without its "v"). */
	version: string;
	/** Page of the release (what changed, every installer). */
	page: string;
	/** Installer for this computer, or the release page when there is none. */
	download: string;
}

interface GithubRelease {
	tag_name?: string;
	html_url?: string;
	draft?: boolean;
	assets?: { name?: string; browser_download_url?: string }[];
}

/** -1, 0 or 1, comparing "1.2.3" versions number by number ("v" and anything after "-" ignored). */
export function compareVersions(a: string, b: string): number {
	const parts = (version: string) =>
		version
			.replace(/^v/i, "")
			.split("-")[0]
			?.split(".")
			.map((part) => Number.parseInt(part, 10) || 0) ?? [];
	const [left, right] = [parts(a), parts(b)];
	for (let i = 0; i < Math.max(left.length, right.length); ++i) {
		const difference = (left[i] ?? 0) - (right[i] ?? 0);
		if (difference !== 0) return difference < 0 ? -1 : 1;
	}
	return 0;
}

/** "windows" or "unix" (Linux, macOS). */
export function platform(userAgent: string): "windows" | "unix" {
	return /windows/i.test(userAgent) ? "windows" : "unix";
}

/**
 * The computer, for the file to download: the host tells it (s2a_hello "system"); without it, the
 * browser's user agent (a Mac always says "Intel" there, so the processor stays unknown).
 */
export type System = "windows" | "macos-arm64" | "macos-x86_64" | "macos" | "linux";

const hostSystems: readonly string[] = ["windows", "macos-arm64", "macos-x86_64", "linux"];

export function systemOf(userAgent: string, host?: string): System {
	if (host && hostSystems.includes(host)) return host as System;
	if (/windows/i.test(userAgent)) return "windows";
	return /mac os|macintosh/i.test(userAgent) ? "macos" : "linux";
}

/** The release file for `system`: the Windows installer, the Mac disk image of its processor. */
function installerPattern(system: System): RegExp | null {
	switch (system) {
		case "windows":
			return /\.exe$/i;
		case "macos-arm64":
			return /-macos-arm64\.dmg$/i;
		case "macos-x86_64":
			return /-macos-x86_64\.dmg$/i;
		default:
			return null; // .deb or .rpm, or a Mac of unknown processor: the release page tells
	}
}

let hostSystem: System | undefined;

/** Called once the host said which computer it runs on (s2a_hello). */
export function setHostSystem(system: string | undefined): void {
	hostSystem = system && hostSystems.includes(system) ? (system as System) : undefined;
}

/** The newest published release of a GitHub release list, with the installer for `system`. */
export function newestRelease(releases: GithubRelease[], system: System): Release | null {
	const wanted = installerPattern(system);
	let newest: Release | null = null;
	for (const release of releases) {
		if (release.draft || !release.tag_name || !release.html_url) continue;
		const version = release.tag_name.replace(/^v/i, "");
		if (newest && compareVersions(version, newest.version) <= 0) continue;
		const installer = wanted
			? release.assets?.find((asset) => wanted.test(asset.name ?? ""))?.browser_download_url
			: undefined;
		newest = { version, page: release.html_url, download: installer ?? release.html_url };
	}
	return newest;
}

/** The newest release if it is newer than `current`, null when up to date. Throws when GitHub cannot be reached. */
export async function findUpdate(current: string, fetcher: typeof fetch = fetch): Promise<Release | null> {
	const response = await fetcher(releasesApi, { headers: { Accept: "application/vnd.github+json" } });
	if (!response.ok) throw new Error(`GitHub answered ${response.status}`);
	const system = hostSystem ?? systemOf(navigator.userAgent);
	const release = newestRelease((await response.json()) as GithubRelease[], system);
	return release && compareVersions(release.version, current) > 0 ? release : null;
}
