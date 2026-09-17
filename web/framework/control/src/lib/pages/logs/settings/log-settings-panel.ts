import { CommonModule } from '@angular/common';
import { Component, OnDestroy, OnInit, ViewChild, computed, input, output, signal, inject } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatTabsModule } from '@angular/material/tabs';
import { MatToolbar } from '@angular/material/toolbar';
import { IENVIRONMENT, IEnvironment, ProfileStore } from 'jde-spa';
import { LogEntries } from '../log-entry';
import { LogTags, TagLevel } from '../tags/log-tags';
import { IGraphQL } from '../../../services/graphql';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { errorMessage } from '../../../utils/errors';
import { Mutation, MutationType } from '../../../model/ql/mutation';

type TagLevels = Record<string,string>;
//instanceTagLevel answers with the tags grouped under their level: { "Debug":["sql",["socket","client","read"]] }.  A
//combined tag has no name to be an object key by, so it arrives as the array Jde::ToValue produced; tagSeparator is how
//the server spells one as a name (Jde::ToLogTags splits on it), and that is what the editor and the save diff key off.
type LevelTags = Record<string,(string|string[])[]>;
//its `running` column is the instance's own logSetting answer - tag->level per sink, `default` included - or null when
//the instance is not connected (InstanceTagLevelAwait.cpp).  A sink it does not run comes back {}.
type RunningLevels = { [type:string]:TagLevels }|null;
type InstanceLevels = { [type:string]:LevelTags|RunningLevels|undefined } & { running?:RunningLevels };
const tagSeparator = ".";//Jde::TagSeparator, fwk log/logTags.h - config keys and instance rows are spelled with it too.
//every catalogue is ordered on the name the row shows, not the wire spelling — tagName reorders a combined tag's
//parts, so "read.server.socket" shows as "Socket.Server.Read" and a bare sort() files it under R.
const byName = ( a:string, b:string )=>LogEntries.tagName( a ).localeCompare( LogEntries.tagName(b) );
const flatten = ( group:LevelTags|undefined ):TagLevels=>{
	const y:TagLevels = {};
	for( const [level, tags] of Object.entries(group ?? {}) )
		for( const tag of tags ?? [] )
			y[Array.isArray(tag) ? tag.join(tagSeparator) : tag] = level;
	return y;
};
//What a sink's tab shows (install-issues #19): every tag the instance reports a level for, and every override saved for it,
//default first and the rest by shown name.  The level is the running one where the instance reports the tag - an override
//it has applied reads at the level it applied; only one it has not (the instance offline when it was saved, or the push
//lost) reads at the stored level - and the mark is whether a stored override exists for the tag.
export const mergeSink = ( running:TagLevels|undefined, overrides:TagLevels ):TagLevel[]=>{
	const tags = new Set( [...Object.keys(running ?? {}), ...Object.keys(overrides)] );
	return [...tags]
		.sort( (a,b)=>a==LogTags.defaultTag ? -1 : b==LogTags.defaultTag ? 1 : byName(a, b) )
		.map( tag=>({ tag, level: running?.[tag] ?? overrides[tag], override: tag in overrides }) );
};
//the overrides as the editor will report them at load - what an unedited Save has to diff to nothing.
export const overridesOf = ( rows:TagLevel[] ):TagLevels=>Object.fromEntries( rows.filter(r=>r.override).map(r=>[r.tag, r.level]) );
//What a Save does (install-issues #20 - the line used to say a change waits for a restart).  The rows are written and pushed
//to the running instance at once (updateInstanceTagLevel → pushRuntime), and every product re-reads them at start
//(IApp::LoadLogSettings), so a change is both live and kept.  An instance that did not answer for its levels cannot be
//pushed to either; it reads the rows when it next starts.
export const saveHint = ( instanceAnswered:boolean ):string=>instanceAnswered
	? "Changes are pushed to the running instance at once and saved for its next start."
	: "The instance did not answer. Changes are saved and take effect when it next starts.";

@Component({
	selector: 'log-settings',
	templateUrl: './log-settings-panel.html',
	styleUrls: ['./log-settings-panel.scss'],
	imports: [CommonModule, MatButtonModule, MatTabsModule, MatToolbar, LogTags]
})
export class LogSettingsPanel implements OnInit, OnDestroy{
	private snackBar:SnackbarService = inject( SnackbarService );
	private environment:IEnvironment = inject( IENVIRONMENT );

	async ngOnInit(){ await this.load(); }
	ngOnDestroy(){ ProfileStore.setTabIndex( 'log-settings', this.tabIndex() ); }

	async load(){
		this.isLoading.set( true );
		this.error.set( null );
		try{
			const log = (m:string)=>console.log(m);
			const [setting, levels] = await Promise.all( [
				this.service().querySingle<{tags:Record<string,number>}>( "logSetting{tags}", null, log ),
				this.service().querySingle<InstanceLevels>( `instanceTagLevel( id:${this.instanceId()} ){ text binary appServer running }`, null, log )
			] );
			this.catalogue.set( ["default", ...Object.keys(setting?.tags ?? {}).sort(byName)] );
			const running = levels?.running ?? null;
			this.runningUnavailable.set( running===null );
			for( const type of LogSettingsPanel.sinks ){
				const sink = mergeSink( running?.[type], flatten(<LevelTags|undefined>levels?.[type]) );
				this.sinks[type] = sink;
				this.snapshot[type] = overridesOf( sink );
				//an answer with no `default` is a logger the instance does not have - the hub has no remote log, a bare app server no
				//appServer column at all (undefined: unknown, not absent).
				const reported = running?.[type];
				this.sinkNote[type] = reported && !(LogTags.defaultTag in reported)
					? `This instance runs no ${LogSettingsPanel.sinkNames[type]} log. The rows are overrides saved for it; they apply to nothing.`
					: null;
			}
			this.isLoading.set( false );
		}
		catch( e ){
			this.error.set( errorMessage(e, "Could not load log settings.") );//errorMessage, not `${e}`: an HttpErrorResponse and a {error:IError} rejection both render "[object Object]".  Without this the panel stays behind isLoading and renders blank
			this.isLoading.set( false );
			this.snackBar.exception( "Could not load log settings.", e );
		}
	}
	async save(){
		try{
			const args:Record<string,unknown> = {};
			for( const [type, child] of <[string,LogTags][]>[ ["text", this.text], ["binary", this.binary], ["appServer", this.remote] ] ){
				const current = child.entries();
				const previous = this.snapshot[type];
				const diff:Record<string,string|null> = {};
				for( const [tag,level] of Object.entries(current) )
					if( previous[tag]!=level )
						diff[tag] = level;
				for( const tag of Object.keys(previous) )
					if( !(tag in current) )
						diff[tag] = null;
				//a record per override - a combined tag is an array, which is no object key.
				if( Object.keys(diff).length )
					args[type] = Object.entries(diff).map( ([tag,level])=>({ tags: tag.split(tagSeparator), level }) );
			}
			if( Object.keys(args).length ){
				await this.service().mutate( new Mutation('instanceTagLevel', this.instanceId(), args, MutationType.Update), (m)=>console.log(m) );
				//re-read rather than keep the edited rows: a removed override's tag is back at its configured level (install-issues
				//#21), which only the instance can say, and an override it applied reads at the level it applied.
				await this.load();
			}
			this.onSave.emit();
		}
		catch( e ){
			this.snackBar.exception( "Could not save log settings.", e );
		}
	}
	async cancel(){
		await this.load();
		this.onCancel.emit();
	}

	service = input.required<IGraphQL>();
	instanceId = input.required<number>();
	onSave = output<void>();
	onCancel = output<void>();

	static sinks = ["text", "binary", "appServer"];
	static sinkNames:Record<string,string> = { text: "text", binary: "binary", appServer: "remote" };//as the tabs are labelled.
	sinks:{ [type:string]:TagLevel[] } = { text:[], binary:[], appServer:[] };
	sinkNote:{ [type:string]:string|null } = { text:null, binary:null, appServer:null };
	snapshot:{ [type:string]:TagLevels } = { text:{}, binary:{}, appServer:{} };
	runningUnavailable = signal<boolean>( false );
	saveHint = computed<string>( ()=>saveHint(!this.runningUnavailable()) );
	catalogue = signal<string[]>( [] );
	//break maps to _breakLevel (fwk src/log/break.cpp), which only fires with a debugger attached, so it is offered
	//on the text sink alone - the one that shares the local process - and never on a production build.
	textCatalogue = computed<string[]>( ()=>{
		const y = this.catalogue();
		if( this.environment.get<boolean>("production") || !y.length || y.includes(LogSettingsPanel.breakTag) )
			return y;
		return [ y[0], ...y.slice(1).concat(LogSettingsPanel.breakTag).sort(byName) ];//load() heads the catalogue with 'default', which is not a tag and stays first
	});
	error = signal<string|null>( null );
	isLoading = signal<boolean>( true );
	tabIndex = signal<number>( ProfileStore.tabIndex('log-settings') );
	static breakTag = "break";

	@ViewChild('binary') binary!: LogTags;
	@ViewChild('remote') remote!: LogTags;
	@ViewChild('text') text!: LogTags;
}
