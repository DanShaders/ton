// deno run --allow-read --allow-write ./fixProfiles.ts
// deno run --allow-all ./fixProfiles.ts
// ^ to read files from WSL

const originalLocation = '\\\\wsl.localhost\\Ubuntu-24.04\\home\\nns2009\\telegram\\build';

const fixes = {
	'profile.ts': 'functionTimings',
	'account_transactions_times.ts': 'account_transactions_times',
	'one_transaction_times.ts': 'one_transaction_times',
};

for (const [filename, exportName] of Object.entries(fixes)) {
	try {
		const originalPath = originalLocation + '\\' + filename;

		const originalContent = Deno.readTextFileSync(originalPath);
		const modifiedContent = `export const ${exportName} = {` + '\n' + originalContent + '\n' + '};\n';
		Deno.writeTextFileSync(filename, modifiedContent);
		Deno.removeSync(originalPath);
		
		console.log(`File ${filename} has been modified!`);
	} catch (error) {
		console.error(`Error processing file ${filename}:`, error);
	}
}



