package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class RunMessage extends ClientMessage {
    private String roomId;
    private String requestId;
    private String language;
    private String sourceCode;
    private String stdin;
    private ResourceLimitsDto limits;

    public RunMessage() {
        setType("run");
    }

    public RunMessage(String roomId, String requestId, String language, String sourceCode, String stdin, ResourceLimitsDto limits) {
        setType("run");
        this.roomId = roomId;
        this.requestId = requestId;
        this.language = language;
        this.sourceCode = sourceCode;
        this.stdin = stdin;
        this.limits = limits;
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

    public String getLanguage() {
        return language;
    }

    public void setLanguage(String language) {
        this.language = language;
    }

    public String getSourceCode() {
        return sourceCode;
    }

    public void setSourceCode(String sourceCode) {
        this.sourceCode = sourceCode;
    }

    public String getStdin() {
        return stdin;
    }

    public void setStdin(String stdin) {
        this.stdin = stdin;
    }

    public ResourceLimitsDto getLimits() {
        return limits;
    }

    public void setLimits(ResourceLimitsDto limits) {
        this.limits = limits;
    }
}
