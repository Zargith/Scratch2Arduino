import "@mantine/core/styles.css";

import { MantineProvider } from "@mantine/core";
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import { App } from "@/App";
import { ErrorBoundary } from "@/ErrorBoundary";
import { cssVariablesResolver, theme } from "@/theme/theme";

const root = document.getElementById("root");
if (!root) throw new Error("index.html has no #root element");

createRoot(root).render(
	<StrictMode>
		<MantineProvider theme={theme} cssVariablesResolver={cssVariablesResolver} defaultColorScheme="light">
			<ErrorBoundary>{(attempt) => <App key={attempt} />}</ErrorBoundary>
		</MantineProvider>
	</StrictMode>
);
