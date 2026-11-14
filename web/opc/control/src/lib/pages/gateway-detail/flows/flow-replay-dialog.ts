import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';

export type FlowReplayDialogData = { name:string; version:number; now?:Date };
export type FlowReplayAsked = { from:Date; to:Date; mode:'dry'|'commit' };

//a time as a datetime-local input holds it, local and to the minute
export const localInput = ( d:Date ):string=>{
	const pad = ( n:number )=>String( n ).padStart( 2, '0' );
	return `${d.getFullYear()}-${pad( d.getMonth()+1 )}-${pad( d.getDate() )}T${pad( d.getHours() )}:${pad( d.getMinutes() )}`;
};
//what is wrong with a replay's range, or nothing:  each a time, in order, and none of it to come, so its records are all written
export function replayProblem( from:string, to:string, now:Date ):string|undefined{
	const start = new Date( from ), end = new Date( to );
	if( !from || isNaN( start.getTime() ) )
		return 'From is a time.';
	if( !to || isNaN( end.getTime() ) )
		return 'To is a time.';
	if( start>=end )
		return 'From is before To.';
	return end>now ? 'To is no later than now:  a replay reads what is stored.' : undefined;
}

//A replay's range and mode (docs/flows/spec.md *Replay*):  the flow's current version over its sources' history from one time to
//another, dry, every sink captured into the trace, or commit, its derived variables written into history over the range.
@Component({
	template: `
		<h2 mat-dialog-title>Replay {{ data.name }}</h2>
		<mat-dialog-content>
			<p>Version {{ data.version }} runs over its sources' history on the stream's clock, enabled or not, beside the live flow.</p>
			<div class="fields">
				<mat-form-field subscriptSizing="dynamic">
					<mat-label>From</mat-label>
					<input matInput type="datetime-local" [value]="from()" (input)="from.set($any($event.target).value)" required>
				</mat-form-field>
				<mat-form-field subscriptSizing="dynamic">
					<mat-label>To</mat-label>
					<input matInput type="datetime-local" [value]="to()" (input)="to.set($any($event.target).value)" required>
				</mat-form-field>
				<mat-form-field subscriptSizing="dynamic" class="mode">
					<mat-label>Mode</mat-label>
					<mat-select [value]="mode()" (selectionChange)="mode.set($event.value)">
						<mat-option value="dry">Dry:  every sink captured, nothing written</mat-option>
						<mat-option value="commit">Commit:  its variables written into history</mat-option>
					</mat-select>
				</mat-form-field>
			</div>
			@if( mode()=='commit' ){
				<p class="warn" role="status">A committed run clears each variable's history over the range as it goes and writes its own values:  what the collector stored there, and what an earlier run wrote, is gone.  Every other sink stays captured.</p>
			}
			@if( problem(); as p ){
				<p class="problem" role="alert">{{ p }}</p>
			}
		</mat-dialog-content>
		<mat-dialog-actions>
			<button matButton="outlined" type="button" (click)="dialogRef.close()">Cancel</button>
			<button matButton="filled" type="button" (click)="apply()" [disabled]="!!problem()">Replay</button>
		</mat-dialog-actions>`,
	styles: `
		p{ margin: 0 0 .75rem; font: var(--mat-sys-body-medium); }
		.fields{ display: grid; grid-template-columns: repeat( 2, minmax(0, 1fr) ); gap: .5rem .75rem; margin-bottom: .75rem; mat-form-field{ width: 100%; } .mode{ grid-column: 1 / -1; } }
		.warn{ color: var(--mat-sys-on-surface-variant); }
		.problem{ color: var(--mat-sys-error); }`,
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatInputModule, MatSelectModule]
})
export class FlowReplayDialog{
	dialogRef = inject<MatDialogRef<FlowReplayDialog,FlowReplayAsked|undefined>>( MatDialogRef );
	data = inject<FlowReplayDialogData>( MAT_DIALOG_DATA );
	#now = this.data.now ?? new Date();
	from = signal( localInput( new Date( this.#now.getTime()-3_600_000 ) ) );//the last hour
	to = signal( localInput( new Date( this.#now.getTime()-60_000 ) ) );//the minute before this one, which is all stored
	mode = signal<'dry'|'commit'>( 'dry' );
	problem = computed( ()=>replayProblem( this.from(), this.to(), new Date() ) );
	apply():void{
		if( !this.problem() )
			this.dialogRef.close( { from: new Date( this.from() ), to: new Date( this.to() ), mode: this.mode() } );
	}
	static open( dialog:MatDialog, data:FlowReplayDialogData ):Promise<FlowReplayAsked|undefined>{
		const ref = dialog.open<FlowReplayDialog,FlowReplayDialogData,FlowReplayAsked|undefined>( FlowReplayDialog, {data, width: '520px', maxWidth: '95vw', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( y=>resolve( y ) ) );
	}
}