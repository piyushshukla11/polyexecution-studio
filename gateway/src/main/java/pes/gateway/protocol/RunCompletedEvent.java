package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class RunCompletedEvent extends ServerEvent {
    private String roomId;
    private String requestId;
    private String status;
    private String stdout;
    private String stderr;
    private Integer exitCode;
    private Integer signal;
    private Long wallMs;
    private Long cpuMs;
    private Long peakMemoryKb;

    public RunCompletedEvent() {
        super("run.completed");
    }

    public RunCompletedEvent(String roomId, String requestId, String status, String stdout, String stderr,
                             Integer exitCode, Integer signal, Long wallMs, Long cpuMs, Long peakMemoryKb) {
        super("run.completed");
        this.roomId = roomId;
        this.requestId = requestId;
        this.status = status;
        this.stdout = stdout;
        this.stderr = stderr;
        this.exitCode = exitCode;
        this.signal = signal;
        this.wallMs = wallMs;
        this.cpuMs = cpuMs;
        this.peakMemoryKb = peakMemoryKb;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public String getRequestId() {
        return requestId;
    }

    public void setRequestId(String requestId) {
        this.requestId = requestId;
    }

    public String getStatus() {
        return status;
    }

    public void setStatus(String status) {
        this.status = status;
    }

    public String getStdout() {
        return stdout;
    }

    public void setStdout(String stdout) {
        this.stdout = stdout;
    }

    public String getStderr() {
        return stderr;
    }

    public void setStderr(String stderr) {
        this.stderr = stderr;
    }

    public Integer getExitCode() {
        return exitCode;
    }

    public void setExitCode(Integer exitCode) {
        this.exitCode = exitCode;
    }

    public Integer getSignal() {
        return signal;
    }

    public void setSignal(Integer signal) {
        this.signal = signal;
    }

    public Long getWallMs() {
        return wallMs;
    }

    public void setWallMs(Long wallMs) {
        this.wallMs = wallMs;
    }

    public Long getCpuMs() {
        return cpuMs;
    }

    public void setCpuMs(Long cpuMs) {
        this.cpuMs = cpuMs;
    }

    public Long getPeakMemoryKb() {
        return peakMemoryKb;
    }

    public void setPeakMemoryKb(Long peakMemoryKb) {
        this.peakMemoryKb = peakMemoryKb;
    }
}
