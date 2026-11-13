// "Report a problem": opens a GitHub issue form already filled with what the editor knows.
export const ISSUES_URL = "https://github.com/Zargith/Scratch2Arduino/issues/new";

export interface ReportContext {
	/** Installed version ("" outside the application). */
	version: string;
	/** Board name as shown in the board menu, or "" when unknown. */
	board: string;
	/** Error text to put in the description (crash screen), or "". */
	details?: string;
}

/** Readable operating system from the webview user agent. */
export function systemName(userAgent: string): string {
	if (/windows/i.test(userAgent)) return "Windows";
	if (/mac os x|macintosh/i.test(userAgent)) return "macOS";
	if (/linux|x11/i.test(userAgent)) return "Linux";
	return "";
}

/** Link to the "Report a problem" issue form with the fields the editor can fill. */
export function reportUrl(context: ReportContext, userAgent: string): string {
	const query = new URLSearchParams({ template: "bug_report.yml" });
	if (context.version) query.set("version", context.version);
	const system = systemName(userAgent);
	if (system) query.set("os", system);
	if (context.board) query.set("board", context.board);
	if (context.details) {
		// Long stack traces would make the link too long for some browsers.
		const details = context.details.length > 1500 ? `${context.details.slice(0, 1500)}\n[...]` : context.details;
		query.set("what-happened", `\n\n\`\`\`\n${details}\n\`\`\``);
	}
	return `${ISSUES_URL}?${query.toString()}`;
}
