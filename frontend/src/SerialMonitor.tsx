// Serial monitor panel. Outside Live mode it connects to the board's port and shows what the
// board prints (Serial.println); in Live mode it shows what the blocks write instead, since the
// Live mode already uses the port. Uploading frees the port: the monitor pauses, then
// reconnects by itself once the upload is over.
import { ActionIcon, Button, Card, CloseButton, Group, ScrollArea, Select, Text, TextInput } from "@mantine/core";
import { IconPlugConnected, IconPlugConnectedX, IconSend, IconTrash } from "@tabler/icons-react";
import { useEffect, useRef, useState } from "react";

import { bridge, hasHost } from "@/bridge";
import type { Language } from "@/catalog";
import type { MessageKey } from "@/i18n";

const bauds = [9600, 19200, 57600, 115200];

/** Frees the port; does nothing in a plain browser (pnpm dev), where the host is missing. */
const closePort = () => {
	if (hasHost()) void bridge.monitorClose().catch(() => undefined);
};

interface SerialMonitorProps {
	t: (key: MessageKey, params?: Record<string, string>) => string;
	language: Language;
	port: string | null;
	/** Live mode running: show its output, do not touch the port. */
	live: boolean;
	liveLines: string[];
	/** An upload is using the port. */
	busy: boolean;
	onError: (message: string, details?: string) => void;
	onClose: () => void;
}

export function SerialMonitor({ t, language, port, live, liveLines, busy, onError, onClose }: SerialMonitorProps) {
	const [baud, setBaud] = useState(bauds[0] ?? 9600);
	/** The user wants to be connected (kept while an upload pauses the connection). */
	const [wanted, setWanted] = useState(false);
	const [connected, setConnected] = useState(false);
	const [lines, setLines] = useState<string[]>([]);
	const [input, setInput] = useState("");
	const viewport = useRef<HTMLDivElement>(null);
	const report = useRef(onError);
	report.current = onError;

	// Connect when wanted and the port is free; the bridge closes it for uploads and Live mode.
	useEffect(() => {
		if (!wanted || connected || live || busy || !port || !hasHost()) return;
		let cancelled = false;
		void bridge
			.monitorOpen(port, baud, language)
			.then((opened) => {
				if (cancelled) return;
				if (opened.ok) setConnected(true);
				else {
					setWanted(false);
					report.current(opened.message ?? "", opened.details);
				}
			})
			.catch(() => setWanted(false));
		return () => {
			cancelled = true;
		};
	}, [wanted, connected, live, busy, port, baud, language]);

	// While connected: read what arrived.
	useEffect(() => {
		if (!connected) return;
		const timer = setInterval(() => {
			void bridge
				.monitorState()
				.then((state) => {
					if (state.lines) setLines(state.lines);
					if (!state.open) {
						setConnected(false);
						if (state.message) {
							setWanted(false);
							report.current(state.message, state.details);
						}
					}
				})
				.catch(() => setConnected(false));
		}, 200);
		return () => clearInterval(timer);
	}, [connected]);

	// An upload or the Live mode takes the port.
	useEffect(() => {
		if (busy || live) setConnected(false);
	}, [busy, live]);

	// Closing the panel frees the port.
	useEffect(() => closePort, []);

	const shown = live ? liveLines : lines;
	useEffect(() => {
		viewport.current?.scrollTo({ top: viewport.current.scrollHeight });
	}, [shown]);

	const disconnect = () => {
		setWanted(false);
		setConnected(false);
		closePort();
	};

	const send = () => {
		if (!connected || input === "") return;
		void bridge.monitorSend(input).catch(() => undefined);
		setInput("");
	};

	return (
		<Card withBorder shadow="sm" w={360} style={{ display: "flex", flexDirection: "column", gap: 8 }}>
			<Group justify="space-between" wrap="nowrap">
				<Text fw={700} size="sm">
					{t("monitor.title")}
				</Text>
				<CloseButton size="sm" aria-label={t("dialog.cancel")} onClick={onClose} />
			</Group>
			{live ? (
				<Text size="xs" c="dimmed">
					{t("monitor.live")}
				</Text>
			) : (
				<Group gap="xs" wrap="nowrap">
					<Select
						aria-label={t("monitor.speed")}
						data={bauds.map((value) => ({ value: String(value), label: `${value} bauds` }))}
						value={String(baud)}
						onChange={(value) => value && setBaud(Number(value))}
						disabled={wanted}
						allowDeselect={false}
						size="xs"
						w={130}
					/>
					{wanted ? (
						<Button size="xs" variant="light" color="pink" leftSection={<IconPlugConnectedX size={14} />} onClick={disconnect}>
							{t("monitor.disconnect")}
						</Button>
					) : (
						<Button
							size="xs"
							variant="light"
							leftSection={<IconPlugConnected size={14} />}
							disabled={!port}
							onClick={() => setWanted(true)}
						>
							{t("monitor.connect")}
						</Button>
					)}
					<ActionIcon variant="default" size="md" ml="auto" aria-label={t("monitor.clear")} onClick={() => setLines([])}>
						<IconTrash size={14} />
					</ActionIcon>
				</Group>
			)}
			{!live && wanted && busy && (
				<Text size="xs" c="dimmed">
					{t("monitor.paused")}
				</Text>
			)}
			<ScrollArea h={220} viewportRef={viewport} type="auto" style={{ background: "var(--mantine-color-default-hover)", borderRadius: 4 }}>
				{shown.length === 0 ? (
					<Text size="xs" c="dimmed" p="xs">
						{t("monitor.empty")}
					</Text>
				) : (
					<Text size="xs" p="xs" style={{ fontFamily: "Space Mono, monospace", whiteSpace: "pre-wrap", wordBreak: "break-word" }}>
						{shown.join("\n")}
					</Text>
				)}
			</ScrollArea>
			{!live && (
				<Group gap="xs" wrap="nowrap">
					<TextInput
						aria-label={t("monitor.placeholder")}
						placeholder={t("monitor.placeholder")}
						value={input}
						onChange={(event) => setInput(event.currentTarget.value)}
						onKeyDown={(event) => event.key === "Enter" && send()}
						disabled={!connected}
						size="xs"
						style={{ flex: 1 }}
					/>
					<ActionIcon variant="filled" size="md" aria-label={t("monitor.send")} disabled={!connected || input === ""} onClick={send}>
						<IconSend size={14} />
					</ActionIcon>
				</Group>
			)}
		</Card>
	);
}
