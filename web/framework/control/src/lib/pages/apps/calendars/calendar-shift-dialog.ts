import { Component, computed, inject, signal } from '@angular/core';
import { form, FormField, required } from '@angular/forms/signals';
import { MatButtonModule } from '@angular/material/button';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogClose, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatTooltipModule } from '@angular/material/tooltip';
import { CalendarPattern, CalendarShift, changedArgs, cycleDayNames, durationText, isWeekly, minutesPerDay, minutesToTime, ShiftArgs, shiftArgsOf, ShiftForm, shiftMinutes, shiftProblem, timeToMinutes, toShiftArgs, toShiftForm } from '../../../model/calendar';
import { errorMessage } from '../../../utils/errors';

//`shift`:  the one edited, none for a new one, which starts at `startAt`, minutes after the production day's start.  `save` sends
//the form's columns, and its throw is the server's refusal, said on the form.
export type CalendarShiftDialogData = { pattern:Pick<CalendarPattern,'cycleDays'|'cycleStart'>; dayStart:number; shift?:CalendarShift; startAt?:number; save:( args:ShiftArgs )=>Promise<unknown> };

//A shift's form (#396):  its name, the cycle days it runs - the week's, or a rotation's by number - its start and end on the
//calendar's wall clock and its breaks, each a start and a length.  The form holds times of day;  the columns are minutes from the
//production day's start and from the shift's (model/calendar.ts).  It checks what it can first, and the server's refusal - an
//overlap with another shift - shows under the fields with the dialog still open.
@Component({
	templateUrl: './calendar-shift-dialog.html',
	styleUrl: './calendar-dialog.scss',
	imports: [FormField, MatButtonModule, MatCheckboxModule, MatDialogActions, MatDialogClose, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatTooltipModule]
})
export class CalendarShiftDialog{
	//true once a shift was saved
	static open( dialog:MatDialog, data:CalendarShiftDialogData ):Promise<boolean>{
		const ref = dialog.open( CalendarShiftDialog, {data, width: '560px', maxWidth: 'calc(100vw - 16px)', ariaModal: true} );
		return new Promise<boolean>( resolve=>ref.afterClosed().subscribe( y=>resolve( y===true ) ) );
	}
	data = inject<CalendarShiftDialogData>( MAT_DIALOG_DATA );
	#ref = inject( MatDialogRef<CalendarShiftDialog,boolean> );
	readonly dayNames = cycleDayNames( this.data.pattern );
	model = signal<ShiftForm>( this.data.shift ? toShiftForm( this.data.shift, this.data.pattern, this.data.dayStart ) : this.#blank() );
	f = form( this.model, path=>{ required( path.name ); } );
	problem = computed( ()=>shiftProblem( this.model() ) );
	lengthText = computed( ()=>{
		const m = this.model(), minutes = shiftMinutes( m ), start = timeToMinutes( m.start );
		return minutes==undefined || start==undefined ? '' : `${durationText( minutes )}${start+minutes>minutesPerDay ? ', into the next day' : ''}`;
	} );
	//an edit that changes nothing has nothing to send
	changed = computed( ()=>!this.data.shift || !!this.problem() || Object.keys( changedArgs( toShiftArgs( this.model(), this.data.dayStart ), shiftArgsOf( this.data.shift ) ) ).length>0 );
	busy = signal( false );
	error = signal<string|undefined>( undefined );

	//eight hours from `startAt`, on the week's working days or on none of a rotation's
	#blank():ShiftForm{
		const {pattern, dayStart, startAt=0} = this.data;
		return { name: '', days: Array.from( {length: pattern.cycleDays}, ( _, i )=>isWeekly( pattern ) && i<5 ), start: minutesToTime( dayStart+startAt ), end: minutesToTime( dayStart+startAt+480 ), breaks: [] };
	}
	//half an hour, midway through the shift
	addBreak():void{
		const m = this.model(), start = timeToMinutes( m.start ) ?? 0, minutes = shiftMinutes( m ) ?? 0;
		this.model.set( {...m, breaks: [...m.breaks, {start: minutesToTime( start+Math.floor( minutes/120 )*60 ), minutes: 30}]} );
		this.f().markAsDirty();//a write to the model doesn't, and the form says what is wrong only once it is edited
	}
	removeBreak( index:number ):void{
		this.model.update( m=>({...m, breaks: m.breaks.filter( ( _, i )=>i!=index )}) );
		this.f().markAsDirty();
	}
	async save():Promise<void>{
		if( this.problem() || this.busy() )
			return;
		this.busy.set( true );
		this.#ref.disableClose = true;//Escape or the backdrop would close it on a save the server may still take, and the page never read again
		this.error.set( undefined );
		try{
			await this.data.save( toShiftArgs( this.model(), this.data.dayStart ) );
			this.#ref.close( true );
		}
		catch( e ){
			this.error.set( errorMessage(e) );
		}
		finally{
			this.#ref.disableClose = false;
			this.busy.set( false );
		}
	}
}