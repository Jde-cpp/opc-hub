import {Component,model} from '@angular/core';
//import { BrowserModule } from '@angular/platform-browser'
import { MatFormFieldModule } from '@angular/material/form-field';
import {MatSelectModule} from '@angular/material/select';
import { ELogLevel } from 'jde-proto/Log';
import { LogLevelInfo, logLevels } from '../../pages/logs/log-levels';

@Component({
    selector: 'severity-picker',
    templateUrl: './severity-picker.html',
    imports: [MatFormFieldModule, MatSelectModule]
})
export class SeverityPicker{
	onSelectionChange( value:ELogLevel ){
		this.level.set( value );//model() emits levelChange on set, and only on a set from HERE
	}
	//model(), so `levelChange` fires for the user's pick and NOT for the parent writing [level].  The old @Input setter
	//emitted on every write after the first, so a parent-driven update bounced straight back at it (review3 C7).
	level = model.required<ELogLevel>();

	options:Pick<LogLevelInfo,'level'|'name'>[] = [...logLevels, {level: ELogLevel.NoLog, name: 'None'}];
}