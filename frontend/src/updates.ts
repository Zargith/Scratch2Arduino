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

/** "windows" or "unix" (Linux, macOS: the .sh installer). */
export function platform(userAgent: string): "windows" | "unix" {
	return /windows/i.test(userAgent) ? "windows" : "unix";
}

/** The newest published release of a GitHub release list, with the installer for `system`. */
export function newestRelease(releases: GithubRelease[], system: "windows" | "unix"): Release | null {
	let newest: Release | null = null;
	for (const release of releases) {
		if (release.draft || !release.tag_name || !release.html_url) continue;
		const version = release.tag_name.replace(/^v/i, "");
		if (newest && compareVersions(version, newest.version) <= 0) continue;
		const wanted = system === "windows" ? /\.exe$/i : /\.sh$/i;
		const installer = release.assets?.find((asset) => wanted.test(asset.name ?? ""))?.browser_download_url;
		newest = { version, page: release.html_url, download: installer ?? release.html_url };
	}
	return newest;
}

/** The newest release if it is newer than `current`, null when up to date. Throws when GitHub cannot be reached. */
export async function findUpdate(current: string, fetcher: typeof fetch = fetch): Promise<Release | null> {
	const response = await fetcher(releasesApi, { headers: { Accept: "application/vnd.github+json" } });
	if (!response.ok) throw new Error(`GitHub answered ${response.status}`);
	const release = newestRelease((await response.json()) as GithubRelease[], platform(navigator.userAgent));
	return release && compareVersions(release.version, current) > 0 ? release : null;
}
