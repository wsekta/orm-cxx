import { Journal } from './core.mjs';
import { loadConfig } from './main.mjs';
const argument = name => process.argv[process.argv.indexOf(name) + 1];
if (!['--config', '--branch', '--sha', '--thread'].every(name => process.argv.includes(name))) throw new Error('Usage: node bridge/register.mjs --config <file> --branch <branch> --sha <SHA> --thread <thread UUID>');
const config = loadConfig(argument('--config'));
const journal = new Journal(config.journalPath);
journal.register(config.repository, argument('--branch'), argument('--sha'), argument('--thread'));
journal.close();
console.log('CI destination registered.');
