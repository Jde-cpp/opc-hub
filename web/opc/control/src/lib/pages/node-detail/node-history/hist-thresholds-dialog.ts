import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { errorMessage } from 'jde-framework';
import { histMemberIndexed, histPathText, HistMemberThresholds, HistThreshold, histThresholdArgs, histThresholdColumns, HistThresholdFields, HistThresholdName, histThresholdNames, HistThresholdOverride, histThresholdProblem, HistThresholds, histThresholdSources, histThresholdText } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { ExNodeId } from '../../../model/ex-node-id';
import { HistThresholdFieldsEditor } from '../../gateway-detail/hist-admin/hist-threshold-fields';

export type HistThresholdsDialogData = {
	group:number;
	name:string;//the group's, for the title
	nodes:Variable[];//the page's variables, which name the members among them
	load:()=>Promise<HistThresholds>;//historyThresholds, asked on opening and again after each override
	save:( id:number, changes:HistThresholdOverride )=>Promise<void>;//updateHistGroupNode;  a throw is the gateway's refusal
};
//The edit form's fields, by column (model/hist.ts HistThresholdFields):  what the member's own row says, as text, and blank where it inherits.
export function ownFields( m:HistMemberThresholds ):HistThresholdFields{
	const y = <HistThresholdFields>{};
	for( const name of histThresholdNames ){
		const t = m.thresholds.find( t=>t.name==name );
		y[name] = t && t.source=='node' && t.value!=null ? String( t.value ) : '';
	}
	return y;
}
//What an edit changes:  each column whose field differs from what the member's own row says - a value, or null for one cleared
//back to inheriting.  A column left as it was isn't sent, so the row keeps inheriting where it did.
export function thresholdChanges( m:HistMemberThresholds, fields:HistThresholdFields ):HistThresholdOverride{ return histThresholdArgs( fields, ownFields( m ) ); }

//A group's member list with its thresholds (#338):  each member's effective Deviation, Format, Min interval, Max interval and
//Stepped, marked with where each comes from - the member's own row, its template member, the group, or the default - and, for
//a user who may update the rows, an edit of any of them:  a value typed overrides what it inherits, and a field cleared puts the
//row back to inheriting.  An enumeration's deviation isn't applied whatever the row says, and its two cells say so.  The list is
//asked again after each override, so a cell shows what the gateway resolved, not what was typed.
@Component({
	templateUrl: './hist-thresholds-dialog.html',
	styleUrls: ['../../gateway-detail/hist-admin/hist-admin-shared.scss', './hist-thresholds-dialog.scss'],
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatIconModule, MatProgressBarModule, MatTableModule, MatTooltipModule, HistThresholdFieldsEditor]
})
export class HistThresholdsDialog{
	constructor(){ this.reload(); }
	dialogRef = inject<MatDialogRef<HistThresholdsDialog,void>>( MatDialogRef );
	data = inject<HistThresholdsDialogData>( MAT_DIALOG_DATA );
	readonly columns = histThresholdColumns;
	rows = signal<HistMemberThresholds[]>( [] );
	canUpdate = signal( false );
	loading = signal( false );
	error = signal<string|undefined>( undefined );//the gateway's refusal of the list, or of an override
	editing = signal<HistMemberThresholds|undefined>( undefined );
	fields = signal<HistThresholdFields>( <HistThresholdFields>{} );
	busy = signal( false );
	displayedColumns = computed<string[]>( ()=>['member', ...histThresholdNames, ...(this.canUpdate() ? ['actions'] : [])] );
	problem = computed<string|undefined>( ()=>this.editing() ? histThresholdProblem( this.fields() ) : undefined );
	changes = computed<HistThresholdOverride>( ()=>{ const m = this.editing(); return m && !this.problem() ? thresholdChanges( m, this.fields() ) : {}; } );
	changed = computed( ()=>Object.keys( this.changes() ).length>0 );

	async reload(){
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const list = await this.data.load();
			this.rows.set( list.nodes );
			this.canUpdate.set( list.update );
		}
		catch( e ){
			this.error.set( errorMessage(e) );
		}
		finally{
			this.loading.set( false );
		}
	}
	//a member by its name on the page where it is one of the page's variables, else its template path, else its node
	memberName( m:HistMemberThresholds ):string{
		const variable = histMemberIndexed( m.node ) ? this.data.nodes.find( v=>v.key==m.node.key ) : undefined;
		return variable?.name ?? ( m.path ? histPathText( m.path ) : this.nodeText( m.node ) );
	}
	memberDetail( m:HistMemberThresholds ):string{
		const where = m.path ? `Template path ${histPathText( m.path )}` : "Added by hand";
		return `${where}.  Node ${this.nodeText( m.node )}.  Row ${m.id}.`;
	}
	//by the server's index, or as the row spells it until the collector has that:  the URI, its index left out
	nodeText( id:ExNodeId ):string{ return histMemberIndexed( id ) ? id.toString() : JSON.stringify( {nsu: id.nsu, ...id.toJson(), ns: undefined} ); }
	threshold( m:HistMemberThresholds, name:HistThresholdName ):HistThreshold|undefined{ return m.thresholds.find( t=>t.name==name ); }
	//the deviation and its format are moot for an enumeration:  its Int32 values aren't a quantity (spec *Thresholds*)
	moot( m:HistMemberThresholds, name:HistThresholdName ):boolean{ return m.enumeration===true && ( name=='exceptionDeviation' || name=='exceptionDeviationFormat' ); }
	//the deviation and its format where the group doesn't apply them:  moot, or a percent format the node has no usable range for,
	//which an edit of either can still put right
	unapplied( m:HistMemberThresholds, name:HistThresholdName ):boolean{
		return this.moot( m, name ) || ( m.band=='noRange' && ( name=='exceptionDeviation' || name=='exceptionDeviationFormat' ) );
	}
	//the range a percent format takes, which a noRange member lacks
	rangeName( m:HistMemberThresholds ):string{ return this.threshold( m, 'exceptionDeviationFormat' )?.value==2 ? 'InstrumentRange' : 'EURange'; }
	cellText( m:HistMemberThresholds, name:HistThresholdName ):string{
		const t = this.threshold( m, name );
		return !t ? '' : this.moot( m, name ) ? 'n/a' : histThresholdText( t );
	}
	cellTooltip( m:HistMemberThresholds, name:HistThresholdName ):string{
		const t = this.threshold( m, name );
		if( !t )
			return '';
		const from = `From ${histThresholdSources[t.source]}`;
		if( this.moot( m, name ) )
			return `${from}, ${histThresholdText( t )}:  not applied - an enumeration stores every change.`;
		if( this.unapplied( m, name ) )
			return `${from}.  Not applied - the node has no usable ${this.rangeName( m )}, so every change is stored.`;
		return m.band=='waiting' && name=='exceptionDeviation' ? `${from}.  The band waits until the server has described the node.` : `${from}.`;
	}
	sourceText( m:HistMemberThresholds, name:HistThresholdName ):string{ return this.threshold( m, name )?.source ?? ''; }
	edit( m:HistMemberThresholds ){
		this.editing.set( m );
		this.fields.set( ownFields( m ) );
		this.error.set( undefined );
	}
	cancel(){ this.editing.set( undefined ); }
	//the member's fields that are off, and what each blank one inherits, for the editor
	mootFields = computed<HistThresholdName[]>( ()=>{ const m = this.editing(); return m ? histThresholdNames.filter( n=>this.moot( m, n ) ) : []; } );
	hints = computed<Partial<Record<HistThresholdName,string>>>( ()=>{
		const m = this.editing();
		return Object.fromEntries( histThresholdNames.map( n=>[n, m && this.moot( m, n ) ? 'Not applied to an enumeration.' : this.inheritsHint( n )] ) );
	} );
	//what the field inherits while blank:  the effective value and its source, unless the row's own is what is shown
	inheritsHint( name:HistThresholdName ):string{
		const m = this.editing();
		const t = m ? this.threshold( m, name ) : undefined;
		if( !t )
			return '';
		return t.source=='node' ? "Blank inherits from the template, the group or the default." : `Inherits ${histThresholdText( t ) || 'none'} from ${histThresholdSources[t.source]}.`;
	}
	async apply(){
		const m = this.editing();
		const changes = this.changes();
		if( !m || !Object.keys( changes ).length || this.busy() )
			return;
		this.busy.set( true );
		this.dialogRef.disableClose = true;
		this.error.set( undefined );
		try{
			await this.data.save( m.id, changes );
			this.editing.set( undefined );
			await this.reload();
		}
		catch( e ){
			this.error.set( errorMessage(e) );
		}
		finally{
			this.busy.set( false );
			this.dialogRef.disableClose = false;
		}
	}
	static open( dialog:MatDialog, data:HistThresholdsDialogData ):Promise<void>{
		const ref = dialog.open<HistThresholdsDialog,HistThresholdsDialogData,void>( HistThresholdsDialog, {data, width: '960px', maxWidth: '95vw', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( ()=>resolve() ) );
	}
}