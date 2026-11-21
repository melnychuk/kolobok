// Count the app's own reported statuses, including its shared seen/done state.
/** @param {{status: string | null | undefined}[]} agents */
export function summarize(agents) {
  const counts = { working: 0, blocked: 0, done: 0 };
  for (const { status } of agents) {
    if (status === "working" || status === "blocked" || status === "done") counts[status]++;
  }
  return counts;
}
