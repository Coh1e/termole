termole.registerCommand("复制为 Markdown 引用", () => {
    const selection = termole.getSelection();
    const lines = selection.split(/\r\n|\r|\n/);
    // A selection that crosses rows includes the empty cells to the right.
    const quote = lines.map((line, index) => {
        const text = index < lines.length - 1 ? line.replace(/ +$/, "") : line;
        return "> " + text;
    }).join("\r\n");
    termole.writeClipboard(quote);
});
