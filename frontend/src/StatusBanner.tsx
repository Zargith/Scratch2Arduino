// The message of the last action (Check, Upload, Save...), over the workspace.
// Audit #211: the state was told by the colour only (WCAG 1.4.1), and fixed offsets made the box
// overlap or vanish when the page is zoomed in (1.4.10).
import { Alert, Code, Loader, ScrollArea, Spoiler, Text } from "@mantine/core";
import { IconAlertTriangle, IconCircleCheck, IconInfoCircle } from "@tabler/icons-react";
import type { CSSProperties } from "react";

import "@/StatusBanner.css";
import type { MessageKey } from "@/i18n";

export interface Status {
	kind: "info" | "success" | "error";
	message: string;
	details?: string;
}

/** Room to leave free around the box (palette on the left, panels on the right...). */
export interface StatusSpace {
	left: number;
	right: number;
	/** CSS length: the virtual board's panel can be in vh. */
	bottom: string;
}

interface StatusBannerProps {
	status: Status;
	busy: boolean;
	space: StatusSpace;
	t: (key: MessageKey) => string;
	onClose: () => void;
}

const look = {
	error: { color: "orange", Icon: IconAlertTriangle, title: "status.kind_error" },
	success: { color: "green", Icon: IconCircleCheck, title: "status.kind_success" },
	info: { color: "blue", Icon: IconInfoCircle, title: "status.kind_info" },
} as const satisfies Record<Status["kind"], { color: string; Icon: unknown; title: MessageKey }>;

/** A number per message: a new message, even with the same words, is a new alert to announce. */
const messageIds = new WeakMap<Status, number>();
let lastMessageId = 0;
function messageId(status: Status): number {
	let id = messageIds.get(status);
	if (id === undefined) {
		id = ++lastMessageId;
		messageIds.set(status, id);
	}
	return id;
}

export function StatusBanner({ status, busy, space, t, onClose }: StatusBannerProps) {
	const { color, Icon, title } = look[status.kind];
	// The offsets are CSS variables: StatusBanner.css drops them when the window is narrow (zoom).
	const place = {
		"--s2a-status-left": `${space.left}px`,
		"--s2a-status-right": `${space.right}px`,
		"--s2a-status-bottom": space.bottom,
	} as CSSProperties;
	return (
		<div className="s2a-status" style={place}>
			<Alert
				// Remounted for each message: screen readers announce a live region reliably when it
				// appears, not when its role changes (info -> error) or the same words come again.
				key={messageId(status)}
				color={color}
				// An error interrupts the screen reader; other messages wait for it to finish.
				role={status.kind === "error" ? "alert" : "status"}
				title={t(title)}
				withCloseButton
				closeButtonLabel={t("status.close")}
				onClose={onClose}
				icon={busy ? <Loader size="xs" aria-hidden /> : <Icon size={20} aria-hidden />}
			>
				<Text size="sm" style={{ whiteSpace: "pre-wrap", wordBreak: "break-word" }}>
					{status.message}
				</Text>
				{status.details && (
					<Spoiler maxHeight={0} showLabel={t("status.details")} hideLabel={t("status.details")}>
						<ScrollArea.Autosize mah={160}>
							<Code block style={{ whiteSpace: "pre-wrap", wordBreak: "break-word" }}>
								{status.details}
							</Code>
						</ScrollArea.Autosize>
					</Spoiler>
				)}
			</Alert>
		</div>
	);
}
