import { Component, computed, input, model } from '@angular/core';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatSelectModule } from '@angular/material/select';
import { MatTooltipModule } from '@angular/material/tooltip';
import { deviationFormats, histThresholdColumns, HistThresholdFields, HistThresholdName, histThresholdProblem } from '../../../model/hist';

//The five threshold columns as a form (spec *Templates*):  a group's defaults, a template member's own values, a node's on its
//way in.  Text, blank where the row inherits (model/hist.ts HistThresholdFields) - the thresholds dialog's editor, as a control the
//pages share.  `hints` says what a blank field means per column;  `blank` is the selects' word for it.
@Component({
	selector: 'hist-threshold-fields',
	template: `
		<div class="fields">
			@for( column of columns; track column.name ){
				<mat-form-field>
					<mat-label>{{ column.label }}@if( column.unit ){ ({{ column.unit }})}</mat-label>
					@switch( column.kind ){
						@case( 'format' ){
							<mat-select [value]="fields()[column.name]" (selectionChange)="set(column.name, $event.value)" [disabled]="disabled()">
								<mat-option value="">{{ blank() }}</mat-option>
								@for( format of formats; track $index ){
									<mat-option [value]="$index + ''">{{ format }}</mat-option>
								}
							</mat-select>
						}
						@case( 'boolean' ){
							<mat-select [value]="fields()[column.name]" (selectionChange)="set(column.name, $event.value)" [disabled]="disabled()">
								<mat-option value="">{{ blank() }}</mat-option>
								<mat-option value="true">true</mat-option>
								<mat-option value="false">false</mat-option>
							</mat-select>
						}
						@default{
							<!--text, not number:  a number input reports a partial entry as blank, which the binding would write back over it-->
							<input matInput type="text" inputmode="decimal" [value]="fields()[column.name]" (input)="set(column.name, $any($event.target).value)" [placeholder]="blank()" [disabled]="disabled()">
						}
					}
					<mat-hint [matTooltip]="column.hint">{{ hints()[column.name] ?? column.hint }}</mat-hint>
				</mat-form-field>
			}
		</div>
		@if( problem(); as p ){
			<p class="problem" role="alert">{{ p }}</p>
		}`,
	styles: `
		:host{ display: block; }
		.fields{
			display: grid;
			grid-template-columns: repeat( auto-fit, minmax(170px, 1fr) );
			gap: .5rem .75rem;
			mat-form-field{ width: 100%; }
		}
		.problem{
			margin: 0;
			color: var(--mat-sys-error);
			font: var(--mat-sys-body-small);
		}`,
	imports: [MatFormFieldModule, MatInputModule, MatSelectModule, MatTooltipModule]
})
export class HistThresholdFieldsEditor{
	fields = model.required<HistThresholdFields>();
	disabled = input( false );
	hints = input<Partial<Record<HistThresholdName,string>>>( {} );
	blank = input( 'Inherit' );
	readonly columns = histThresholdColumns;
	readonly formats = deviationFormats;
	problem = computed( ()=>histThresholdProblem( this.fields() ) );
	set( name:HistThresholdName, text:string ){ this.fields.update( f=>({...f, [name]: text}) ); }
}