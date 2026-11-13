import { Alert, Button, Code, Container, Group, ScrollArea, Stack, Text } from "@mantine/core";
import { Component, type ReactNode } from "react";

import { bridge } from "@/bridge";
import { detectLanguage, translate } from "@/i18n";
import { reportUrl } from "@/report";

interface ErrorBoundaryProps {
	children(attempt: number): ReactNode;
}

interface ErrorBoundaryState {
	error: unknown;
	attempt: number;
}

/**
 * Last line of defence: an unexpected error must never leave the child with a blank window. It
 * explains what happened and starts the editor again (the project that caused it is not reopened).
 */
export class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
	override state: ErrorBoundaryState = { error: null, attempt: 0 };

	static getDerivedStateFromError(error: unknown): Partial<ErrorBoundaryState> {
		return { error: error ?? "unknown error" };
	}

	override componentDidCatch(error: unknown) {
		console.error("the editor stopped", error);
	}

	override render() {
		if (this.state.error === null) return this.props.children(this.state.attempt);
		const language = detectLanguage();
		const error = this.state.error;
		const details = error instanceof Error ? `${error.message}\n\n${error.stack ?? ""}` : String(error);
		return (
			<Container size="sm" py="xl">
				<Alert color="orange" title={translate(language, "crash.title")}>
					<Stack gap="sm">
						<Text size="sm">{translate(language, "crash.message")}</Text>
						<ScrollArea.Autosize mah={240}>
							<Code block style={{ whiteSpace: "pre-wrap", wordBreak: "break-word" }}>
								{details}
							</Code>
						</ScrollArea.Autosize>
						<Group gap="sm">
							<Button onClick={() => this.setState((state) => ({ error: null, attempt: state.attempt + 1 }))}>
								{translate(language, "crash.restart")}
							</Button>
							<Button
								variant="default"
								onClick={() =>
									void bridge
										.openUrl(reportUrl({ version: "", board: "", details }, navigator.userAgent))
										.catch(() => undefined)
								}
							>
								{translate(language, "settings.report")}
							</Button>
						</Group>
					</Stack>
				</Alert>
			</Container>
		);
	}
}
