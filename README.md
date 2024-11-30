# Boiler Control
Boiler temperature compensation for Tado.

Some 4 years ago I installed a Tado setup with 9 zones - one for each main room with 11 tado rad valves. In a somewhat rambling house it has done wonders for efficiency and comfort as each room is separately controlled.

This works with an rugged cast iron boiler of the old school that has outlasted at least 3 modern boilers. 3 years ago I configured the Tado boiler controller to use the legacy analogue 'Wolf' output to drive a temperature controller to create a load optimised circulation temperature. It works but has a wierd low frequency wavy output voltage curve that needed cleaning up. And it was a fiddle to revert back to relay control if needed. As one of my early projects using 4 line lcd displays and matrix keypads for control and setup it became somewhat wieldy to maintain. A verson of it is on my Github for now.

Having read other successes including Terence Eden's blog I created this interface to the Tado API to gather the zone stats once a minute and work out an optimised 'power demand' level from the actual vs setpoint temperatures. Also it gathers the outside temperature - at least according to Tado and it is accurate to a degree or two - as well as the home/away state. This is all used to derive an optimised circulation temperature and hot water heating enabler that is sent via MQTT to the boiler temperature controller. The Tado boiler controller works in simple relay mode.

The temperature optimiser follows the classic straight line for compensation. eg outside 0-15 gives boiler temperature 65-50 and adds on a -3 to +3 shift depending on 0-100% power demand. The aim is to keep the boiler on for longer at a lower temperature to improve efficiency and give stable room temperatures.

The benefit of this approach is that if anything fails the boiler falls back to simple mechanical thermostat control without optimisation.

The boiler temperature contoller has more wizadry for providing efficient opertion. 
