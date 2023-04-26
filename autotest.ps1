Copy-Item .\traces\MSR_trace.txt -Destination .\trace.txt
"'n" | & .\MQSim.exe -i ssdconfig.xml -w workload.xml | Out-File msr_result.txt
Remove-Item .\trace.txt
Copy-Item .\traces\ssdTrace.txt -Destination .\trace.txt
"'n" | & .\MQSim.exe -i ssdconfig.xml -w workload.xml | Out-File ssd_result.txt
Remove-Item .\trace.txt
Copy-Item .\traces\Systor.txt -Destination .\trace.txt
"'n" | & .\MQSim.exe -i ssdconfig.xml -w workload.xml | Out-File Systor_result.txt
Remove-Item .\trace.txt
pause