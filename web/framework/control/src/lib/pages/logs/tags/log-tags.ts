import { CommonModule } from '@angular/common';
import { Component, OnInit, ViewChild, input } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIcon } from '@angular/material/icon';
import { MatSelectModule } from '@angular/material/select';
import { MatTable, MatTableModule } from '@angular/material/table';
import { MatTooltip } from '@angular/material/tooltip';
import { LogEntries } from '../log-entry';
import { SeverityPicker } from '../../../shared/severity-picker/severity-picker';

import { ELogLevel } from 'jde-proto/Log';

//A sink's row as the panel hands it over: the level in wire spelling, and whether it is an override saved for the
//instance (an instance_tag_levels row) or the level the instance reports running with (install-issues #19).
export type TagLevel = { tag:string, level:string, override:boolean };
//configured: the level a non-override row came in with - what removing an override falls back to, and what makes picking
//it again no override.  Unset for a stored override (the instance cannot say what its config had) and for a new row.
export type TagRow = { tag:string, level:ELogLevel, override:boolean, configured?:ELogLevel };

@Component({
	selector: 'log-tags',
	templateUrl: './log-tags.html',
	styleUrls: ['./log-tags.scss'],
	imports: [CommonModule, MatButtonModule, MatFormFieldModule, MatIcon, MatSelectModule, MatTableModule, MatTooltip, SeverityPicker]
})
export class LogTags implements OnInit{
	ngOnInit(){
		const given = this.tags();
		const stored = given.find( t=>t.tag==LogTags.defaultTag );
		this.dataSource = [
			//LogTags() in logTags.h defaults to Information when nothing is stored.  A default the instance did not report has no
			//configured level to fall back to, so it is not an override until the user picks a level - see onLevelChange/entries.
			stored ? LogTags.toRow( stored ) : { tag: LogTags.defaultTag, level: ELogLevel.Information, override: false },
			...given.filter( t=>t.tag!=LogTags.defaultTag ).map( LogTags.toRow ),
			{...LogTags.emptyRow}
		];
	}
	isDefault( row:TagRow ):boolean{ return row.tag==LogTags.defaultTag; }
	//came in at its running level: the tag is fixed (it is the instance's, not a row of ours to repoint), and it can be reverted to.
	isConfigured( row:TagRow ):boolean{ return row.configured!==undefined; }
	//Picking a level is what makes a row an override - unless it is the level the instance already runs the tag at, which is
	//no change to save.  A default the instance did not report has no such level, so any pick counts, Information included:
	//the instance's CONFIGURED default may be something else and choosing Information is then a deliberate change.
	onLevelChange( row:TagRow, level:ELogLevel ){
		row.level = level;
		row.override = row.configured===undefined || level!=row.configured;
	}
	onTagChange( row:TagRow, tag:string ){
		const isNew = !row.tag;
		row.tag = tag;
		if( isNew ){
			this.dataSource.push( {...LogTags.emptyRow} );
			this.table.renderRows();
		}
	}
	//removing an override: a configured tag goes back to the level it came in with; a stored override's row goes, the
	//save then sends `level:null` for it.
	onDelete( row:TagRow ){
		if( row.configured!==undefined ){
			row.level = row.configured;
			row.override = false;
			return;
		}
		this.dataSource.splice( this.dataSource.indexOf(row), 1 );
		this.table.renderRows();
	}
	tagName( tag:string ):string{ return LogEntries.tagName(tag); }
	selectableTags( row:TagRow ):string[]{
		return this.catalogue().filter( t=>t==row.tag || !this.dataSource.some(r=>r.tag==t) );
	}
	//What this sink overrides - the rows the save diffs against the stored ones.  A configured row is NOT one, nor is a default
	//the instance never stored and the user never touched: reporting the synthesized Information made
	//`previous['default']` undefined != 'Information' on every Save - even one with no edits - and wrote a tag-0/Information
	//row for text, binary AND appServer, pinning the instance's default over its configured level across restarts.
	entries():Record<string,string>{
		return Object.fromEntries( this.dataSource
			.filter( r=>r.tag && r.override )
			.map( r=>[r.tag, LogTags.toWire(r.level)] ) );
	}

	tags = input.required<TagLevel[]>();
	catalogue = input.required<string[]>();
	dataSource:TagRow[] = [];
	static emptyRow:TagRow = { tag: "", level: ELogLevel.Information, override: true };//a row the user adds is an override once it has a tag.
	static defaultTag = "default";
	static toRow( t:TagLevel ):TagRow{
		const level = LogTags.fromWire( t.level );
		return { tag: t.tag, level, override: t.override, configured: t.override ? undefined : level };
	}
	static toWire( l:ELogLevel ):string{ return l==ELogLevel.NoLog || l==ELogLevel.LogLevelNone ? "None" : ELogLevel[l]; }
	static fromWire( s:string ):ELogLevel{ return s=="None" ? ELogLevel.NoLog : (<any>ELogLevel)[s] ?? ELogLevel.Information; }
	@ViewChild('table', {static: true}) table!: MatTable<TagRow>;
}
