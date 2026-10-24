'use strict';

import * as fs from 'fs';
import * as path from 'path';

//VS Code's built-in C++ grammar matches a lambda's trailing return type as `\S+`, so in `[]()->T{` the type swallows the `{` and the
//return-type region runs on until a `{` preceded by whitespace, coloring every line in between as a type.  This writes a copy of the
//installed grammar with that match stopped at `{` to syntaxes/, which package.json contributes as source.cpp over the built-in.
const relative = path.join( 'resources', 'app', 'extensions', 'cpp', 'syntaxes', 'cpp.tmLanguage.json' );

function builtIn():string {
	if( process.argv[2] )
		return process.argv[2];
	const cli = process.platform == 'win32' ? 'code.cmd' : 'code';
	for( const dir of (process.env.PATH ?? '').split(path.delimiter) ){
		if( !dir || !fs.existsSync(path.join(dir, cli)) )
			continue;
		const root = path.dirname( path.dirname(fs.realpathSync(path.join(dir, cli))) );
		const subDirs = fs.readdirSync( root, {withFileTypes:true} ).filter( d => d.isDirectory() ).map( d => path.join(root, d.name) );//some installs nest resources/ one level down.
		const found = [root, ...subDirs].map( d => path.join(d, relative) ).find( f => fs.existsSync(f) );
		if( found )
			return found;
	}
	throw new Error( `no ${relative} under the install of '${cli}' on PATH; pass the built-in grammar's path.` );
}

function patch( grammar:any ):void {
	const returnType = grammar.repository?.lambdas?.patterns?.find( (p:any) => p.begin == '->' );
	const type = returnType?.patterns?.find( (p:any) => p.match == '\\S+' );
	if( !type )
		throw new Error( "lambda return-type rule not found; the upstream grammar changed - check whether `[]()->T{` still miscolors." );
	type.match = '[^\\s{]+';
}

const source = builtIn();
const grammar = JSON.parse( fs.readFileSync(source, 'utf8') );
patch( grammar );
const target = path.join( __dirname, '..', '..', 'syntaxes', 'cpp.tmLanguage.json' );
fs.mkdirSync( path.dirname(target), {recursive:true} );
fs.writeFileSync( target, JSON.stringify(grammar) );
console.log( `${source} -> ${target}` );