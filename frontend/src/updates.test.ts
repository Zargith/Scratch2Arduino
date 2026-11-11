import { describe, expect, it } from "vitest";

import { compareVersions, findUpdate, newestRelease, platform } from "@/updates";

const releases = [
	{ tag_name: "v0.8.0", html_url: "https://github.com/Zargith/Scratch2Arduino/releases/tag/v0.8.0", assets: [] },
	{
		tag_name: "v0.10.0",
		html_url: "https://github.com/Zargith/Scratch2Arduino/releases/tag/v0.10.0",
		assets: [
			{ name: "Scratch2Arduino_installer.exe", browser_download_url: "https://github.com/x/installer.exe" },
			{ name: "Scratch2Arduino_installer.sh", browser_download_url: "https://github.com/x/installer.sh" },
		],
	},
	{ tag_name: "v0.11.0", html_url: "https://github.com/x/draft", draft: true },
	{ tag_name: "v0.9.0", html_url: "https://github.com/Zargith/Scratch2Arduino/releases/tag/v0.9.0" },
];

describe("update check", () => {
	it("compares versions number by number", () => {
		expect(compareVersions("0.10.0", "0.9.0")).toBe(1);
		expect(compareVersions("v0.9.0", "0.9.0")).toBe(0);
		expect(compareVersions("0.9.0", "0.9.1")).toBe(-1);
		expect(compareVersions("1.0", "0.99.9")).toBe(1);
	});

	it("picks the newest published release and the installer for the computer", () => {
		expect(newestRelease(releases, "windows")).toEqual({
			version: "0.10.0",
			page: "https://github.com/Zargith/Scratch2Arduino/releases/tag/v0.10.0",
			download: "https://github.com/x/installer.exe",
		});
		expect(newestRelease(releases, "unix")?.download).toBe("https://github.com/x/installer.sh");
		expect(newestRelease([], "unix")).toBeNull();
		expect(platform("Mozilla/5.0 (Windows NT 10.0; Win64; x64)")).toBe("windows");
		expect(platform("Mozilla/5.0 (X11; Linux x86_64)")).toBe("unix");
	});

	it("tells only about a newer release", async () => {
		const github = (async () => new Response(JSON.stringify(releases))) as typeof fetch;
		expect((await findUpdate("0.9.0", github))?.version).toBe("0.10.0");
		expect(await findUpdate("0.10.0", github)).toBeNull();
		const offline = (async () => new Response("", { status: 503 })) as typeof fetch;
		await expect(findUpdate("0.9.0", offline)).rejects.toThrow();
	});
});
