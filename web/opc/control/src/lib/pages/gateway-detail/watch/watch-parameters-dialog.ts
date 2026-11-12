import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatTooltipModule } from '@angular/material/tooltip';
import { EDataChangeTrigger, EDeadbandType, parametersProblem, queueDefault, samplingDefault, WatchFilter, WatchParameters } from '../../../model/watch';

export type WatchParametersDialogData = { name:string; parameters:WatchParameters };
//The form's fields as text, blank where the row asks the gateway's default (spec *Watch list*, Opc.FromClient.proto)
export type WatchParameterFields = { samplingInterval:string; queueSize:string; discardOldest:''|'true'|'false'; trigger:''|'0'|'1'|'2'; deadbandType:'0'|'1'|'2'; deadbandValue:string };

export function fieldsOf( p:WatchParameters ):WatchParameterFields{
	return {
		samplingInterval: p.samplingInterval==undefined ? '' : String( p.samplingInterval ),
		queueSize: p.queueSize==undefined ? '' : String( p.queueSize ),
		discardOldest: p.discardOldest==undefined ? '' : p.discardOldest ? 'true' : 'false',
		trigger: p.filter?.trigger==undefined ? '' : <'0'|'1'|'2'>String( p.filter.trigger ),
		deadbandType: <'0'|'1'|'2'>String( p.filter?.deadbandType ?? EDeadbandType.None ),
		deadbandValue: p.filter ? String( p.filter.deadbandValue ) : '0'
	};
}
//The parameters the fields spell:  a blank field is left out, the gateway's default.  A filter is sent when the trigger is set or a
//deadband is asked - deadband None with no trigger is the server's default, which no filter asks for.  Text that is no number is
//kept as NaN, which parametersProblem names.
export function parametersOf( f:WatchParameterFields ):WatchParameters{
	const y:WatchParameters = {};
	const number = ( text:string )=>text.trim()==='' ? undefined : Number( text );
	const sampling = number( f.samplingInterval ), queue = number( f.queueSize );
	if( sampling!==undefined ) y.samplingInterval = sampling;
	if( queue!==undefined ) y.queueSize = queue;
	if( f.discardOldest ) y.discardOldest = f.discardOldest=='true';
	const deadbandType = <EDeadbandType>Number( f.deadbandType );
	if( f.trigger!=='' || deadbandType!=EDeadbandType.None ){
		const filter:WatchFilter = { deadbandType, deadbandValue: deadbandType==EDeadbandType.None ? 0 : (number( f.deadbandValue ) ?? NaN) };
		if( f.trigger!=='' )
			filter.trigger = <EDataChangeTrigger>Number( f.trigger );
		y.filter = filter;
	}
	return y;
}

//A row's monitoring parameters (spec *Watch list*):  the sampling interval, queue size and discard rule the item asks its server for,
//and the data-change filter - when the item reports, and the deadband a change has to clear.  Blank is the gateway's default.  The
//server may revise the sampling and the queue, and refuse a deadband - the list shows what it answered.
@Component({
	templateUrl: './watch-parameters-dialog.html',
	styles: `
		mat-dialog-content{ container-type: inline-size; }/* the host is the dialog's \`display: contents\`, which no query can ask */
		.fields{
			display: grid;
			grid-template-columns: repeat( 2, minmax(0, 1fr) );
			gap: .5rem .75rem;
			mat-form-field{ width: 100%; }
		}
		.hint{ margin: .75rem 0 0; color: var(--mat-sys-on-surface-variant); font: var(--mat-sys-body-medium); }
		.problem{ color: var(--mat-sys-error); margin: .5rem 0 0; }
		@container (width < 412px){ .fields{ grid-template-columns: minmax(0, 1fr); } }/* room for two 200px fields and the gap, not the viewport (419-watch #14) */`,
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatInputModule, MatSelectModule, MatTooltipModule]
})
export class WatchParametersDialog{
	dialogRef = inject<MatDialogRef<WatchParametersDialog,WatchParameters|undefined>>( MatDialogRef );
	data = inject<WatchParametersDialogData>( MAT_DIALOG_DATA );
	fields = signal<WatchParameterFields>( fieldsOf( this.data.parameters ) );
	parameters = computed<WatchParameters>( ()=>parametersOf( this.fields() ) );
	problem = computed<string|undefined>( ()=>parametersProblem( this.parameters() ) );
	readonly samplingDefault = samplingDefault;
	readonly queueDefault = queueDefault;
	readonly triggers = [ {value: '', label: 'Default (StatusValue)'}, {value: '0', label: 'Status'}, {value: '1', label: 'StatusValue'}, {value: '2', label: 'StatusValueTimestamp'} ];
	readonly deadbands = [ {value: '0', label: 'None'}, {value: '1', label: 'Absolute'}, {value: '2', label: 'Percent of EURange'} ];
	set<K extends keyof WatchParameterFields>( name:K, value:WatchParameterFields[K] ){ this.fields.update( f=>({...f, [name]: value}) ); }
	get deadbandOff():boolean{ return this.fields().deadbandType=='0'; }
	apply(){
		if( !this.problem() )
			this.dialogRef.close( this.parameters() );
	}
	//the parameters set, or undefined for a cancel
	static open( dialog:MatDialog, data:WatchParametersDialogData ):Promise<WatchParameters|undefined>{
		const ref = dialog.open<WatchParametersDialog,WatchParametersDialogData,WatchParameters|undefined>( WatchParametersDialog, {data, width: '520px', maxWidth: '95vw', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( p=>resolve( p ) ) );
	}
}